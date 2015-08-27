#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>

#include "aux_log.h"
#include "prog_config.h"
#include "basic_mysql.h"
#include "buffer.h"
#include "zbx_api.h"

#include "typedef.h"
#include "main.h"

#include "snmp.h"
#include "create.h"
#include "update.h"

namespace {
   conf::config_map ddstech_section = {
      { "host",            { conf::val_type::string } },
      { "username",        { conf::val_type::string } },
      { "password",        { conf::val_type::string } },
      { "port",            { conf::val_type::integer, 3306 } },

      { "devinfo-table",   { conf::val_type::string } },
      { "hierarchy-table", { conf::val_type::string } },
      { "types-table",     { conf::val_type::string } }
   };

   conf::config_map zabbix_section = {
      { "api-url",            { conf::val_type::string } },
      { "username",           { conf::val_type::string } },
      { "password",           { conf::val_type::string } },

      { "community-macro",    { conf::val_type::string, "{$SNMP_COMMUNITY}" } },
      { "autod-macro",        { conf::val_type::string, "{$_AUTO_DEPLOY}" } },
      { "param-macro",        { conf::val_type::string, "{$_AUTO_PARAMS}" } },
      { "lower-macro",        { conf::val_type::string, "{$_LWRL_COUNT}" } },
      { "default-group",      { conf::val_type::string, "unknown" } },

      { "int-templateid",     { conf::val_type::integer } },
      { "icmp_l1_templateid", { conf::val_type::integer } },
      { "icmp_l2_templateid", { conf::val_type::integer } },
      { "icmp_l3_templateid", { conf::val_type::integer } },      
   };

   const char *progname = "zbx_dd";
   const char *conffile = "zbx_dd.conf";
}

conf::config_map config = {
   { "dds-tech-db",      { conf::val_type::section, &ddstech_section } },
   { "zabbix",           { conf::val_type::section, &zabbix_section  } },
   { "snmp_communities", { conf::val_type::multistring } }
};

zbx_api::api_session zbx_sess;
std::vector<uint_t> ping_templates;

bool primary_ip(const std::string &host, basic_mysql &db)
{
   static const char *funcname = "primary_ip";
   const char *hoststring = host.c_str();

   uint_t count = db.query(true, "select is_primary from %s where ip = '%s'",
         config["dds-tech-db"]["devinfo-table"].get<conf::string_t>().c_str(), hoststring);

   if (0 == count) logger.error_exit(funcname, "%s: no info from dds-tech DB -> ignored.", hoststring);
   if (1 != count) logger.error_exit(funcname, "%s: somehow received more than ine device from dds-tech DB: %lu", hoststring, count);
   return strtoul(db.get(0, 0), nullptr, 10);
}

int get_zabbix_host(glob_hostdata &hostdata)
{
   static const char *funcname = "get_zabbix_host";

   if (0 == zbx_sess.send_vstr(R"**(
      "method": "host.get",
      "params": {
         "output": [ "hostid" ],
         "selectMacros": [ "macro", "value" ],
         "selectGroups" : [ "groupid" ],
         "selectParentTemplates": [ "templateid" ],
         "filter": { "host": ["%s"] } }
   )**", hostdata.host.c_str())) return 0;

   if (false == zbx_sess.json_get_uint("result[0].hostid", &(hostdata.zbx_host.id)))
      logger.error_exit(funcname, "Cannot get host ID from JSON response.");

   buffer tempstr, jsonpath;
   for (int i = 0; ;i++)
   {
      jsonpath.print("result[0].macros[%d].macro", i);
      if (false == zbx_sess.json_get_str(jsonpath.data(), &tempstr)) break;
      std::string &lstr = hostdata.zbx_host.macros[tempstr.data()];

      jsonpath.print("result[0].macros[%d].value", i);
      if (false == zbx_sess.json_get_str(jsonpath.data(), &tempstr))
         logger.error_exit(funcname, "Failed to get macro value.");
      lstr = tempstr.data();
   }

   // If we found a host and it doesn't have auto-deployer macro, then we have no right to do anything with it.
   // Return all ones in host flags, which means do nothing and exit.
   auto flags_macro = hostdata.zbx_host.macros.find(config["zabbix"]["autod-macro"].get<conf::string_t>());
   if (hostdata.zbx_host.macros.end() == flags_macro) { hostdata.zbx_host.flags.set(); return 1; }
   hostdata.zbx_host.flags = flags_type(flags_macro->second);

   uint_t temp;
   for (int i = 0; ;i++)
   {
      jsonpath.print("result[0].parentTemplates[%d].templateid", i);
      if (false == zbx_sess.json_get_uint(jsonpath.data(), &temp)) break;
      hostdata.zbx_host.templates.insert(temp);

      for (auto id : ping_templates) {
         if (id == temp) hostdata.zbx_host.pingt_id = temp; }
   }

   for (int i = 0; ;i++)
   {
      jsonpath.print("result[0].groups[%d].groupid", i);
      if (false == zbx_sess.json_get_uint(jsonpath.data(), &temp)) break;
      hostdata.zbx_host.groups.insert(temp);
   }
   return 1;
}

void get_device_params(glob_hostdata &hostdata, basic_mysql &db)
{
   static const char *funcname = "get_device_params";

   if (0 == db.query(true, "select name, prefix, ping_level, int_level from %s where objID = '%s'",
            config["dds-tech-db"]["types-table"].get<conf::string_t>().c_str(), hostdata.objid.data()))
   {
      logger.log_message(LOG_CRIT, funcname, "%s: There is no records for objID '%s' in DB.",
            hostdata.host.c_str(), hostdata.objid.data());
      return;
   }

   hostdata.db_devdata.init = true;
   hostdata.db_devdata.devname = db.get(0, 0);
   hostdata.db_devdata.ping_level = strtoul(db.get(0, 2), nullptr, 10);
   hostdata.db_devdata.int_level = strtoul(db.get(0, 3), nullptr, 10);

   if (0 == hostdata.db_devdata.ping_level or 3 < hostdata.db_devdata.ping_level)
   {
      logger.error_exit(funcname, "Received unexepected ping level from DB for '%s': %lu", 
            hostdata.objid.data(), hostdata.db_devdata.ping_level);
   }

   if (interfaces_monitoring_off != hostdata.db_devdata.int_level and 
       interfaces_monitoring_on != hostdata.db_devdata.int_level)
   {
      logger.error_exit(funcname, "Received unexpected int level from DB for '%s': %lu",
            hostdata.objid.data(), hostdata.db_devdata.int_level);
   }

   if (nullptr != db.get(0, 1))
   {
      hostdata.db_devdata.prefix = db.get(0, 1);
      hostdata.db_devdata.prefix += ' ';
   }
}

void get_device_name(glob_hostdata &hostdata, basic_mysql &db)
{
   static const char *funcname = "get_device_name";

   if (0 == db.query(true, "select location from %s where ip = '%s'", 
            config["dds-tech-db"]["devinfo-table"].get<conf::string_t>().c_str(), hostdata.host.c_str()))
   {
      logger.log_message(LOG_WARNING, funcname, "%s: no results for location for this host.", hostdata.host.c_str());
      return;
   }

   hostdata.name = db.get(0, 0);
   hostdata.name += " (";
   hostdata.name += hostdata.host.c_str() + 7;
   hostdata.name += ")";
}

int get_device_uplink(glob_hostdata &hostdata, basic_mysql &db)
{
   static const char *funcname = "get_device_uplink";
   switch (db.query(true, "select ip from %s where remote_ip = '%s'",
            config["zabbix"]["hierarchy-table"].get<conf::string_t>()))
   {
      case 0: logger.log_message(LOG_WARNING, funcname, "%s: cannot obtain uplink device for host", hostdata.host.c_str());
              return 0;
      default: logger.log_message(LOG_WARNING, funcname, "%s: received more than one uplink for host.", hostdata.host.c_str());
               return 0;

      case 1: break;
   }

   hostdata.uplink = db.get(0, 0);
   return 1;
}

int main(int argc, char *argv[])
{
   logger.method = logging::log_method::M_STDE;
   openlog(progname, LOG_PID, LOG_LOCAL7);
   if (2 != argc) logger.error_exit(progname, "%s: should be called with exactly one argument - device IP.", argv[0]);

   // Ignoring CORE devices - no auto deploying for them.
   // NOTE: This should be rewritten to use some config option like 'ignore-target-host'
   // NOTE: Maybe we should check ip string integrity too.
   if (nullptr != strstr(argv[1], "172.17.0.")) return 0;

   try {
      if (0 == conf::read_config(conffile, config))
         logger.error_exit(progname, "Error while reading configuration file.");

      basic_mysql db(config["dds-tech-db"]["host"].get<conf::string_t>(),
                     config["dds-tech-db"]["username"].get<conf::string_t>(),
                     config["dds-tech-db"]["password"].get<conf::string_t>(),
                     config["dds-tech-db"]["port"].get<conf::integer_t>());

      glob_hostdata hostdata(argv[1]);
      if (!primary_ip(hostdata.host, db)) return 0;

      zbx_sess.set_auth(config["zabbix"]["api-url"].get<conf::string_t>(),
                        config["zabbix"]["username"].get<conf::string_t>(),
                        config["zabbix"]["password"].get<conf::string_t>());

      ping_templates.push_back(0);
      ping_templates.push_back(config["zabbix"]["icmp_l1_templateid"].get<conf::integer_t>());
      ping_templates.push_back(config["zabbix"]["icmp_l2_templateid"].get<conf::integer_t>());
      ping_templates.push_back(config["zabbix"]["icmp_l3_templateid"].get<conf::integer_t>());

      if (0 != get_zabbix_host(hostdata) and hostdata.zbx_host.flags.all()) return 0;

      init_snmp(progname);
      netsnmp_ds_set_int(NETSNMP_DS_LIBRARY_ID, NETSNMP_DS_LIB_OID_OUTPUT_FORMAT, NETSNMP_OID_OUTPUT_NUMERIC);

      if (0 != snmp_get_objid(hostdata, SNMP_VERSION_2c) or
          0 != snmp_get_objid(hostdata, SNMP_VERSION_1)) get_device_params(hostdata, db);
      if (hostdata.db_devdata.init) get_device_name(hostdata, db);

      if (0 == hostdata.zbx_host.id) zbx_create_host(hostdata);
      else                           zbx_update_host(hostdata);

      if (! hostdata.zbx_host.flags.test(dig_ping_depend_update))
      {
         if (0 != get_device_uplink(hostdata, db))
            update_icmp_trigdepend(hostdata);
      }


   }

   catch (logging::error &error) { logger.error_exit(progname, "%s: Exception thrown while processing host: %s", argv[1], error.what()); }
   catch (...) { logger.error_exit(progname, "%s: Aborted by generic exception throw.", argv[1]); }

}
