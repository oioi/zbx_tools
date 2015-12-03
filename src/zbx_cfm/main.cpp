#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>

#include <sstream>
#include <algorithm>
#include <vector>

#include "prog_config.h"
#include "basic_mysql.h"

#include "aux_log.h"
#include "zbx_api.h"

#include "main.h"

namespace {
   conf::config_map zabbix_section = {
      { "api-url",      { conf::val_type::string } },
      { "username",     { conf::val_type::string } },
      { "password",     { conf::val_type::string } },
      { "item-history", { conf::val_type::integer, 7 } }
   };

   conf::config_map mysql_section = {
      { "host",      { conf::val_type::string } },
      { "username",  { conf::val_type::string } },
      { "password",  { conf::val_type::string } },
      { "cfm-table", { conf::val_type::string } },
      { "port",      { conf::val_type::integer, 3306 } }
   };

   const char *progname = "zbx_cfm";
   const char *conffile = "zbx_cfm.conf";

   buffer tempbuf;
   zbx_api::api_session zbx_sess;

   std::string action;
   std::string vlan;
   std::string cfm_macro;
}

conf::config_map config = {
   { "mysql",           { conf::val_type::section, &mysql_section  } },
   { "zabbix",          { conf::val_type::section, &zabbix_section } },
   { "snmp-community",  { conf::val_type::string                   } },
   { "cfm-aplname",     { conf::val_type::string,  "CFM"           } },
   { "macro-prefix",    { conf::val_type::string,  "_CFMVLAN_"      } }
};

void snmp_get_objid(hostdata &host)
{
   static const char *funcname = "snmp_get_objid";
   static oid objID[] = { 1, 3, 6, 1, 2, 1, 1, 2, 0 };

   snmp_session snmp_sess;
   snmp_session *sessp;

   snmp_sess_init(&snmp_sess);
   snmp_sess.version = SNMP_VERSION_2c;
   snmp_sess.peername = const_cast<char *>(host.hostname.c_str());
   snmp_sess.community = (u_char *) config["snmp-community"].get<conf::string_t>().c_str();
   snmp_sess.community_len = config["snmp-community"].get<conf::string_t>().size();

   if (nullptr == (sessp = snmp_open(&snmp_sess)))
   {
      int liberr, syserr;
      char *errstr;
      snmp_error(&snmp_sess, &liberr, &syserr, &errstr);
      logger.error_exit(funcname, "%s: Got error when called snmp_open() for host: %s", host.hostname.c_str(), errstr);
   }

   netsnmp_pdu *response;
   netsnmp_pdu *request = snmp_pdu_create(SNMP_MSG_GET);
   snmp_add_null_var(request, objID, sizeof(objID) / sizeof(oid));

   if (STAT_SUCCESS == snmp_synch_response(sessp, request, &response) and
       SNMP_ERR_NOERROR == response->errstat)
   {
      netsnmp_variable_list *vars = response->variables;
      if (SNMP_NOSUCHOBJECT == vars->type)
         logger.error_exit(funcname, "%s: host returned 'no such object' for objID OID", host.hostname.c_str());
      if (ASN_OBJECT_ID != vars->type) logger.error_exit(funcname, "%s: Device returned an unexpected ASN type for ObjID OID.", host.hostname.c_str());

      char *objid_str = new char[max_objid_strlen];
      int len;

      if (-1 == (len = snprint_objid(objid_str, max_objid_strlen, vars->val.objid, vars->val_len / sizeof(oid))))
         logger.error_exit(funcname, "snprint_objid failed. buffer is not large enough?");
      objid_str[len] = '\0';
      host.objid.setmem(objid_str, max_objid_strlen, len);

      snmp_free_pdu(response);
      snmp_close(sessp);
      return;
   }

   logger.error_exit(funcname, "Timeout or error status while getting objID from '%s'", host.hostname.c_str());
}

void get_zbx_hostdata(hostdata &host, bool get_interfaces)
{
   static const char *funcname = "get_zbx_hostdata";

   zbx_sess.send_vstr(R"**(
      "method": "host.get",
      "params": {
         "filter": { "host" : "%s" },
         "output": "hostid",
         "selectInterfaces": [ "interfaceid", "type" ],
         "selectMacros" : [ "macro", "value" ] }
      )**", host.hostname.c_str());

   if (false == zbx_sess.json_get_uint("result[0].hostid", &(host.host_id)))
      logger.error_exit(funcname, "There is no device in Zabbix with hostname '%s'", host.hostname.c_str());

   buffer temp;
   for (int i = 0; ;i++)
   {
      tempbuf.print("result[0].macros[%d].macro", i);
      if (false == zbx_sess.json_get_str(tempbuf.data(), &temp)) break;
      std::string &lstr = host.macros[temp.data()];

      tempbuf.print("result[0].macros[%d].value", i);
      if (false == zbx_sess.json_get_str(tempbuf.data(), &temp))
         logger.error_exit(funcname, "Failed to get macro value.");
      lstr = temp.data();
   }

   if (!get_interfaces) return;
   for (uint_t i = 0, temp = 0; ;i++)
   {
      tempbuf.print("result[0].interfaces[%lu].type", i);
      if (false == zbx_sess.json_get_uint(tempbuf.data(), &temp)) break;

      if (temp != static_cast<uint_t>(zbx_api::host_interface_type::zbx_snmp)) continue;
      tempbuf.print("result[0].interfaces[%lu].interfaceid", i);
      if (false == zbx_sess.json_get_uint(tempbuf.data(), &(host.interface_id)))
         logger.error_exit(funcname, "%s: Can't get interface id from JSON.", host.hostname.c_str());
      break;
   }
   if (0 == host.interface_id) logger.error_exit(funcname, "Host '%s' doesn't have any SNMP interfaces", host.hostname.c_str());
}

void check_cfm_application(hostdata &host)
{
   static const char *funcname = "check_cfm_application";

   zbx_sess.send_vstr(R"**(
      "method": "application.get",
      "params": {
         "output": [ "application_id", "name" ],
         "hostids": "%lu" }
      )**", host.host_id);

   buffer cfm_name;
   for (int i = 0; ; i++)
   {
      tempbuf.print("result[%d].name", i);
      if (false == zbx_sess.json_get_str(tempbuf.data(), &cfm_name)) break;
      if (0 != strcmp(cfm_name.data(), config["cfm-aplname"].get<conf::string_t>().c_str())) continue;

      tempbuf.print("result[%d].applicationid", i);
      if (false == zbx_sess.json_get_uint(tempbuf.data(), &(host.application_id)))
         logger.error_exit(funcname, "Cannot get application ID from JSON response.");
      break;
   }

   if (0 == host.application_id) return;
   zbx_sess.send_vstr(R"**(
      "method": "item.get",
      "params": {
         "hostids": "%lu",
         "applicationids": "%lu",
         "countOutput": true }
      )**", host.host_id, host.application_id);

   if (false == zbx_sess.json_get_uint("result", &(host.item_count)))
      logger.error_exit(funcname, "Cannot get application item count from JSON response");         
}

void snmp_trap_prepare(hostdata &host, basic_mysql &db)
{
   static const std::string vlan_str {"%VLAN%"};
   static const std::string host_str {"%HOST%"};
   static const std::string trap_str {"%TRAP%"};

   db.query(true, "select trap_item, trigger_regex from %s where objID = '%s'",
         config["mysql"]["cfm-table"].get<conf::string_t>().c_str(), host.objid.data());
   host.item_name = db.get(0, 0);
   host.trigger_expr = db.get(0, 1);

   std::size_t pos;

   while (std::string::npos != (pos = host.trigger_expr.find(vlan_str)))
      host.trigger_expr.replace(pos, vlan_str.size(), vlan);

   while (std::string::npos != (pos = host.trigger_expr.find(host_str)))
      host.trigger_expr.replace(pos, host_str.size(), host.hostname);

   while (std::string::npos != (pos = host.trigger_expr.find(trap_str)))
      host.trigger_expr.replace(pos, trap_str.size(), host.item_name);

   pos = std::string::npos;
   while (std::string::npos != (pos = host.trigger_expr.find('"', (std::string::npos == pos) ? 0 : pos + 2)))
      host.trigger_expr.replace(pos, sizeof(char), "\\\"");

   pos = std::string::npos;
   while (std::string::npos != (pos = host.trigger_expr.find('\n', (std::string::npos == pos) ? 0 : pos + 2)))
      host.trigger_expr.replace(pos, sizeof(char), "\\n");

   pos = std::string::npos;
   while (std::string::npos != (pos = host.trigger_expr.find('\r', (std::string::npos == pos) ? 0 : pos + 2)))
      host.trigger_expr.replace(pos, sizeof(char), "\\r");

   zbx_sess.send_vstr(R"**(
      "method": "item.get",
      "params": {
         "hostids": "%lu",
         "filter": { "key_": "%s" },
         "output": [ "itemid" ] }
   )**", host.host_id, host.item_name.c_str());
   host.trap_item_exist = zbx_sess.json_get_uint("result[0].itemid", &(host.item_id));
}

void zbx_sender_prepare(hostdata &host)
{
   tempbuf.print("cfmtrap[%s]", vlan.c_str());
   host.item_name = tempbuf.data();

   tempbuf.print("{%s:cfmtrap[%s].nodata(2m)} = 0", host.hostname.c_str(), vlan.c_str());
   host.trigger_expr = tempbuf.data();
}

void deploy_zabbix_cfm(hostdata &host)
{
   static const char *funcname = "deploy_zabbix_cfm";

   if (0 == host.application_id)
   {
      zbx_sess.send_vstr(R"**(
         "method": "application.create",
         "params": { "name": "%s", "hostid": "%lu" }
      )**", config["cfm-aplname"].get<conf::string_t>().c_str(), host.host_id);

      if (false == zbx_sess.json_get_uint("result.applicationids[0]", &(host.application_id)))
         throw logging::error(funcname, "Looks like CFM application creation failed");
   }

   switch (host.alert_type)
   {
      case cfm_alert_type::zbx_sender:
         zbx_sess.send_vstr(R"**(
            "method": "item.create",
            "params": {
               "name": "CFM VLAN %s",
               "key_": "%s",
               "hostid": "%lu",
               "type": %u,
               "value_type": %u,
               "history": %d,
               "applications": [ "%lu" ] }
         )**", vlan.c_str(), host.item_name.c_str(), host.host_id,
         static_cast<unsigned int>(zbx_api::item::type::zbx_trapper),
         static_cast<unsigned int>(zbx_api::item::value_type::text_t),
         config["zabbix"]["item-history"].get<conf::integer_t>(), host.application_id);
         break;

      case cfm_alert_type::snmp_trap:
         if (!(host.trap_item_exist)) 
         {
            zbx_sess.send_vstr(R"**(
               "method": "item.create",
               "params": {
                  "name": "CFM Trap Item",
                  "key_": "%s",
                  "hostid": "%lu",
                  "type": %u,
                  "value_type": %u,
                  "history": %d,
                  "interfaceid": "%lu",
                  "applications": [ "%lu" ] }
            )**", host.item_name.c_str(), host.host_id,
            static_cast<unsigned int>(zbx_api::item::type::snmp_trap),
            static_cast<unsigned int>(zbx_api::item::value_type::log_t),
            config["zabbix"]["item-history"].get<conf::integer_t>(),
            host.interface_id, host.application_id);
         }
         break;
   }

   if (!(host.trap_item_exist))
   {
      if (false == zbx_sess.json_get_uint("result.itemids[0]", &(host.item_id)))
         throw logging::error(funcname, "Cannot get item ID from JSON response.");
   }

   zbx_sess.send_vstr(R"**(
      "method": "trigger.create",
      "params": {
         "description": "CFM Warning Vlan %s",
         "expression": "%s",
         "priority": %u }
   )**", vlan.c_str(), host.trigger_expr.c_str(), 
   static_cast<unsigned int>(zbx_api::trigger_severity::average));   

   if (false == zbx_sess.json_get_uint("result.triggerids[0]", &(host.trigger_id)))
      throw logging::error(funcname, "Cannot get trigger ID from JSON response.");

   std::string &lstr = host.macros[cfm_macro];
   tempbuf.print("%lu;%lu", host.item_id, host.trigger_id);
   lstr = tempbuf.data();

   tempbuf.clear();
   for (auto &macro : host.macros)
      tempbuf.append(R"**({"macro": "%s", "value": "%s"},)**", macro.first.c_str(), macro.second.c_str());
   tempbuf.pop_back();

   zbx_sess.send_vstr(R"**(
      "method": "host.update",
      "params": {
         "hostid" : "%lu",
         "macros": [ %s ] }
   )**", host.host_id, tempbuf.data());
}

void delete_zabbix_cfm(hostdata &host)
{
   static const char *funcname = "delete_zabbix_cfm";

   uint_t item_id {}, trigger_id {};
   std::istringstream in(host.macros[cfm_macro]);
   std::string token;

   for (int i = 0; std::getline(in, token, ';'); i++)
   {
      switch (i)
      {
         case 0: item_id = std::stoul(token, nullptr, 10); break;
         case 1: trigger_id = std::stoul(token, nullptr, 10); break;
         default: throw logging::error(funcname, "%s: Unexpected value number in CFM macro.", host.hostname.c_str());
      }
   }

   zbx_sess.send_vstr(R"**(
      "method": "item.get",
      "params": {
         "itemids": "%lu",
         "selectTriggers" : "count" }
   )**", item_id);

   uint_t triggers;
   if (false == zbx_sess.json_get_uint("result[0].triggers", &triggers))
      throw logging::error(funcname, "Cannot get triggers count from JSON response");

   host.macros.erase(cfm_macro);
   tempbuf.clear();

   for (auto &macro : host.macros)
      tempbuf.append(R"**({"macro": "%s", "value": "%s"},)**", macro.first.c_str(), macro.second.c_str());
   tempbuf.pop_back();

   zbx_sess.send_vstr(R"**(
      "method": "host.update",
      "params": {
         "hostid" : "%lu",
         "macros": [ %s ] }
   )**", host.host_id, tempbuf.data());   

   zbx_sess.send_vstr(R"**(
      "method": "trigger.delete",
      "params": [ "%lu" ]
   )**", trigger_id);
   if (triggers - 1 > 0) return;

   zbx_sess.send_vstr(R"**(
      "method": "item.delete",
      "params": [ "%lu" ]
   )**", item_id);
   if (host.item_count - 1 > 0) return;

   zbx_sess.send_vstr(R"**(
      "method": "application.delete",
      "params": [ "%lu" ]
   )**", host.application_id);
}

void delete_hosts_cfm(std::vector<hostdata> &hosts)
{
   static const char *funcname = "delete_hosts_cfm";

   for (auto &host : hosts)
   {
      get_zbx_hostdata(host, false);
      if (host.macros.end() == host.macros.find(cfm_macro))
         logger.error_exit(funcname, "%s: vlan %s macro doesn't exist. And probably didn't or macro was lost.",
                           host.hostname.c_str(), vlan.c_str());
      check_cfm_application(host);
   }

   std::stringstream done;
   try {
      for (auto &host : hosts)
      {
         delete_zabbix_cfm(host);
         done << host.hostname << std::endl;
      }
   }

   catch (logging::error &error)
   {
      logger.error_exit(funcname, "Exception thrown while processing hosts: %s\nSuccessfully processed hosts:\n%s",
        error.what(), done.str().c_str());
   }
   catch (...) { logger.error_exit(funcname, "Aborted by generic exception throw. Something really bad happened."); }
}

void create_hosts_cfm(std::vector<hostdata> &hosts, basic_mysql &db)
{
   static const char *funcname = "create_hosts_cfm";

   for (auto &host : hosts)
   {
      snmp_get_objid(host);
      get_zbx_hostdata(host, true);

      if (host.macros.end() != host.macros.find(cfm_macro))
         logger.error_exit(funcname, "%s: vlan %s macro already exists. Either you're trying to reinit host in a wrong way or there's a mistake.",
                           host.hostname.c_str(), vlan.c_str());

      if (0 == db.query(true, "select alert_type from %s where objID = '%s'", 
               config["mysql"]["cfm-table"].get<conf::string_t>().c_str(), host.objid.data()))
         logger.error_exit(funcname, "No entries in cfm-type table for devices with objID '%s'", host.objid.data());

      switch (host.alert_type = static_cast<cfm_alert_type>(strtoul(db.get(0, 0), nullptr, 10)))
      {
         default: logger.error_exit(funcname, "Received unexpected CFM alert type from DB: %d", static_cast<int>(host.alert_type));
         case cfm_alert_type::snmp_trap: snmp_trap_prepare(host, db); break;
         case cfm_alert_type::zbx_sender: zbx_sender_prepare(host); break;
      }
      check_cfm_application(host);
   }

   std::stringstream done;
   try {
      for (auto &host : hosts)
      {
         deploy_zabbix_cfm(host);
         done << host.hostname << std::endl;
      }
   }

   catch (logging::error &error)
   {
      logger.error_exit(funcname, "Exception thrown while processing hosts: %s\nSuccessfully processed hosts:\n%s",
        error.what(), done.str().c_str());
   }
   catch (...) { logger.error_exit(funcname, "Aborted by generic exception throw. Something really bad happened."); }   
}


int main(int argc, char *argv[])
{
   logger.method = logging::log_method::M_STDO;
   if (argc < 4) logger.error_exit(progname, "Wrong argument count.\nUsage: zbx_cfm [add | delete] [vlan] [ip1 ip2 ...]");

   action = argv[1];
   if ("add" != action and "del" != action)
      logger.error_exit(progname, "Wrong action: %s", action.c_str());

   vlan = argv[2];
   std::for_each(vlan.begin(), vlan.end(),
      [](const char ch) { if (0 == isdigit(ch)) logger.error_exit(progname, "Incorrect vlan: %s", vlan.c_str()); });

   if (0 == conf::read_config(conffile, config))
      logger.error_exit(progname, "Error while reading configuration file.");

   tempbuf.print("{$%s%s}", config["macro-prefix"].get<conf::string_t>().c_str(), vlan.c_str());
   cfm_macro = tempbuf.data();

   std::vector<hostdata> hosts;
   for (int i = 3; i < argc; i++) hosts.push_back(hostdata(argv[i]));

   zbx_sess.set_auth(config["zabbix"]["api-url"].get<conf::string_t>(),
                     config["zabbix"]["username"].get<conf::string_t>(),
                     config["zabbix"]["password"].get<conf::string_t>());

   if ("del" == action)
   {
      delete_hosts_cfm(hosts);
      return 0;
   }

   basic_mysql db(config["mysql"]["host"].get<conf::string_t>().c_str(),
                  config["mysql"]["username"].get<conf::string_t>().c_str(),
                  config["mysql"]["password"].get<conf::string_t>().c_str(),
                  config["mysql"]["port"].get<conf::integer_t>());
   init_snmp(progname);
   netsnmp_ds_set_int(NETSNMP_DS_LIBRARY_ID, NETSNMP_DS_LIB_OID_OUTPUT_FORMAT, NETSNMP_OID_OUTPUT_NUMERIC);

   create_hosts_cfm(hosts, db);
   return 0;   
}
