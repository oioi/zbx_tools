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
   configuration::config_map zabbix_section = {
      { "api-url",      { true, static_cast<char *>(nullptr) } },
      { "username",     { true, static_cast<char *>(nullptr) } },
      { "password",     { true, static_cast<char *>(nullptr) } },
      { "item-history", { false, 7 } }
   };

   configuration::config_map mysql_section = {
      { "host",      { true, static_cast<char *>(nullptr) } },
      { "username",  { true, static_cast<char *>(nullptr) } },
      { "password",  { true, static_cast<char *>(nullptr) } },
      { "cfm-table", { true, static_cast<char *>(nullptr) } },
      { "port",      { false, 3306 } }
   };

   const char *progname = "zbx_cfm";
   const char *conffile = "zbx_cfm.conf";

   buffer tempbuf;
   zbx_api::api_session zbx_sess;

   std::string action;
   std::string vlan;
}

configuration::config_map config = {
   { "mysql",           { true, &mysql_section  } },
   { "zabbix",          { true, &zabbix_section } },
   { "snmp-community",  { true, static_cast<char *>(nullptr) } },
   { "cfm-aplname",     { false, "CFM" } }
};

void snmp_get_objid(hostdata &host)
{
   static const char *funcname = "snmp_get_objid";
   static oid objID[] = { 1, 3, 6, 1, 2, 1, 1, 2, 0 };

   struct snmp_session snmp_sess;
   struct snmp_session *sessp = nullptr;

   snmp_sess_init(&snmp_sess);
   snmp_sess.version = SNMP_VERSION_2c;
   snmp_sess.peername = const_cast<char *>(host.hostname.c_str());

   // Fuck that shit, net-snmp
   snmp_sess.community = (u_char *) config["snmp-community"].str().c_str();
   snmp_sess.community_len = config["snmp-community"].str().size();

   if (nullptr == (sessp = snmp_open(&snmp_sess)))
   {
      int liberr, syserr;
      char *errstr;
      snmp_error(&snmp_sess, &liberr, &syserr, &errstr);
      logger.error_exit(funcname, "%s: Got error when called snmp_open() for host: %s", host.hostname.c_str(), errstr);
   }

   netsnmp_pdu *response = nullptr;
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
         throw logging::error(funcname, "snprint_objid failed. buffer is not large enough?");
      objid_str[len] = '\0';
      host.objid.setmem(objid_str, max_objid_strlen, len);

      snmp_free_pdu(response);
      snmp_close(sessp);
      return;
   }

   logger.error_exit(funcname, "Timeout or error status while getting objID from '%s'", host.hostname.c_str());
}
void get_zbx_hostdata(hostdata &host)
{
   static const char *funcname = "get_zbx_hostdata";

   zbx_sess.send_vstr(R"**(
      "method": "host.get",
      "params": {
         "filter": { "host" : "%s" },
         "output": "hostid",
         "selectInterfaces": [ "interfaceid", "type" ] }
      )**", host.hostname.c_str());

   if (false == zbx_sess.json_get_uint("result[0].hostid", &(host.host_id)))
      logger.error_exit(funcname, "There is no device in Zabbix with hostname '%s'", host.hostname.c_str());

   for (uint_t i = 0, temp = 0; ;i++)
   {
      tempbuf.print("result[0].interfaces[%lu].type", i);
      if (false == zbx_sess.json_get_uint(tempbuf.data(), &temp)) break;

      if (temp != static_cast<uint_t>(zbx_api::host_interface_type::ZBX_SNMP)) continue;
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
   for (uint_t i = 0; ; i++)
   {
      tempbuf.print("result[%lu].name", i);
      if (false == zbx_sess.json_get_str(tempbuf.data(), &cfm_name)) break;
      if (0 != strcmp(cfm_name.data(), config["cfm-aplname"].str().c_str())) continue;

      tempbuf.print("result[%lu].applicationid", i);
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
   static const std::string vlan_str = "%VLAN%";
   static const std::string host_str = "%HOST%";

   db.query(true, "select trap_item, trigger_regex from %s where objID = '%s'",
         config["mysql"]["cfm-table"].str().c_str(), host.objid.data());
   host.item_name = db.get(0, 0);
   host.trigger_expr = db.get(0, 1);

   std::size_t pos;

   while (std::string::npos != (pos = host.trigger_expr.find(vlan_str)))
      host.trigger_expr.replace(pos, vlan_str.size(), vlan);

   while (std::string::npos != (pos = host.trigger_expr.find(host_str)))
      host.trigger_expr.replace(pos, host_str.size(), host.hostname);

   pos = 0;
   while (std::string::npos != (pos = host.trigger_expr.find('"', pos + 2)))
      host.trigger_expr.replace(pos, 1, "\\\"");

   zbx_sess.send_vstr(R"**(
      "method": "item.get",
      "params": {
         "hostids": "%lu",
         "search": { "key_": "%s" } ,
         "countOutput": true }
   )**", host.host_id, host.item_name.c_str());

   uint_t temp;
   zbx_sess.json_get_uint("result", &temp);
   if (0 != temp) host.trap_item_exist = true;
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
      )**", config["cfm-aplname"].str().c_str(), host.host_id);

      if (false == zbx_sess.json_get_uint("result.applicationids[0]", &(host.application_id)))
         throw logging::error(funcname, "Looks like CFM application creation failed");
   }

   switch (host.alert_type)
   {
      case cfm_alert_type::ZBX_SENDER: 
         zbx_sess.send_vstr(R"**(
            "method": "item.create",
            "params": {
               "name": "CFM VLAN %s",
               "key_": "%s",
               "hostid": "%lu",
               "type": %u,
               "value_type": %u,
               "history": %lu,
               "applications": [ "%lu" ] }
         )**", vlan.c_str(), host.item_name.c_str(), host.host_id,
         static_cast<unsigned int>(zbx_api::item::type::ZBX_TRAPPER),
         static_cast<unsigned int>(zbx_api::item::value_type::TEXT),
         config["zabbix"]["item-history"].intv(), host.application_id);
         break;

      case cfm_alert_type::SNMP_TRAP:
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
                  "history": %lu,
                  "interfaceid": "%lu",
                  "applications": [ "%lu" ] }
            )**", host.item_name.c_str(), host.host_id,
            static_cast<unsigned int>(zbx_api::item::type::SNMP_TRAP),
            static_cast<unsigned int>(zbx_api::item::value_type::LOG),
            config["zabbix"]["item-history"].intv(),
            host.interface_id, host.application_id);
         }
         break;
   }

   zbx_sess.send_vstr(R"**(
      "method": "trigger.create",
      "params": {
         "description": "CFM Warning Vlan %s",
         "expression": "%s",
         "priority": %u }
   )**", vlan.c_str(), host.trigger_expr.c_str(), 
   static_cast<unsigned int>(zbx_api::trigger_severity::AVERAGE));
}

void delete_zabbix_cfm(const hostdata &host)
{
   static const char *funcname = "delete_zabbix_cfm";

   zbx_sess.send_vstr(R"**(
      "method": "item.get",
      "params": {
         "hostids": "%lu",
         "output" : [ "itemid", "triggers" ],
         "filter": { "key_": "%s" },
         "selectTriggers" : "count" }
   )**", host.host_id, host.item_name.c_str());

   uint_t item_id, triggers;
   if (false == zbx_sess.json_get_uint("result[0].itemid", &item_id))
      throw logging::error(funcname, "Cannot get itemid from JSON response.\nProbably input VLAN or host is wrong.");
   if (false == zbx_sess.json_get_uint("result[0].triggers", &triggers))
      throw logging::error(funcname, "Cannot get triggers count from JSON response");

   zbx_sess.send_vstr(R"**(
      "method": "trigger.get",
      "params": {
         "hostids": "%lu",
         "filter" : { "description": "CFM Warning Vlan %s" },
         "output": "triggerid" }
   )**", host.host_id, vlan.c_str());

   uint_t trigger_id;
   if (false == zbx_sess.json_get_uint("result[0].triggerid", &trigger_id))
      throw logging::error(funcname, "Cannot get triggerid from JSON response\nProbably input VLAN or host is wrong.");

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

   if (0 == configuration::read_config(conffile, config))
      logger.error_exit(progname, "Error while reading configuration file.");

   std::vector<hostdata> hosts;
   for (int i = 3; i < argc; i++) hosts.push_back(hostdata(argv[i]));

   basic_mysql db(config["mysql"]["host"].str().c_str(), 
                  config["mysql"]["username"].str().c_str(), 
                  config["mysql"]["password"].str().c_str(),
                  config["mysql"]["port"].intv());
   init_snmp(progname);
   netsnmp_ds_set_int(NETSNMP_DS_LIBRARY_ID, NETSNMP_DS_LIB_OID_OUTPUT_FORMAT, NETSNMP_OID_OUTPUT_NUMERIC);

   curl_global_init(CURL_GLOBAL_ALL);
   zbx_sess.set_auth(config["zabbix"]["api-url"].str(),
                     config["zabbix"]["username"].str(),
                     config["zabbix"]["password"].str());

   // First we do everything that can be done without actually creating/deleting anything.
   for (auto &host: hosts)
   {
      // Since some of zabbix devices can be added manually without discovering, 
      // we have to actually poll devices to determine device type.
      snmp_get_objid(host);
      get_zbx_hostdata(host);

      if (0 == db.query(true, "select alert_type from %s where objID = '%s'", 
               config["mysql"]["cfm-table"].str().c_str(), host.objid.data()))
         logger.error_exit(progname, "No entries in cfm-type table for devices with objID '%s'", host.objid.data());      


      switch (host.alert_type = static_cast<cfm_alert_type>(strtoul(db.get(0, 0), nullptr, 10)))
      {
         default: logger.error_exit(progname, "Received unexpected CFM alert type from DB: %d", static_cast<int>(host.alert_type));
         case cfm_alert_type::SNMP_TRAP: snmp_trap_prepare(host, db); break;
         case cfm_alert_type::ZBX_SENDER: zbx_sender_prepare(host); break;
      }
      check_cfm_application(host);      
   }

   std::stringstream processed;
   try {
      for (auto &host : hosts)
      {
         if ("add" == action)      deploy_zabbix_cfm(host);
         else if ("del" == action) delete_zabbix_cfm(host);
         processed << host.hostname << std::endl;
      }
   }
   catch (logging::error &error)
   {
      logger.error_exit(progname, "Exception thrown while processing hosts: %s\nSuccessfully processed hosts:\n%s",
        error.what(), processed.str().c_str());
   }
   catch (...) { logger.error_exit(progname, "Aborted by generic exception throw. Something really bad happened."); }

   curl_global_cleanup();
   return 0;
}
