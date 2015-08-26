#include <sstream>

#include "prog_config.h"
#include "aux_log.h"
#include "zbx_api.h"

#include "update.h"

buffer build_host_macrostr(const glob_hostdata &hostdata)
{
   buffer macros;
   for (auto &macro : hostdata.zbx_host.macros)
      macros.append(R"**({"macro": "%s", "value": "%s"},)**", macro.first.c_str(), macro.second.c_str());
   macros.pop_back();
   return macros;
}

void parse_params_macro(glob_hostdata &hostdata)
{
   static const char *funcname = "parse_params_macro";

   auto macro = hostdata.zbx_host.macros.find(config["zabbix"]["param-macro"].get<conf::string_t>());
   if (hostdata.zbx_host.macros.end() == macro)
      logger.error_exit(funcname, "%s: host has no parameters macro, but marked as auto. Possibly broken.", hostdata.host.c_str());

   std::istringstream in(macro->second);
   std::string token;

   for (int i = 0; std::getline(in, token, ';'); i++)
   {
      switch (i)
      {
         case 0: hostdata.zbx_devdata.ping_level = std::stoul(token, nullptr, 10); break;
         case 1: hostdata.zbx_devdata.int_level = std::stoul(token, nullptr, 10); break;
         case 2: hostdata.zbx_devdata.devname = token; break;
         default: logger.error_exit(funcname, "%s: unexpected token count in parameters macro: %s", 
                        hostdata.host.c_str(), macro->second.c_str());
      }
   }

   if (0 == hostdata.zbx_devdata.ping_level or 3 < hostdata.zbx_devdata.ping_level)
   {
      logger.error_exit(funcname, "%s: received unexepected ping level from Zabbix: %lu",
            hostdata.host.c_str(), hostdata.zbx_devdata.ping_level);
   }

   if (interfaces_monitoring_off != hostdata.zbx_devdata.int_level and 
       interfaces_monitoring_on != hostdata.zbx_devdata.int_level)
   {
      logger.error_exit(funcname, "%s: received unexpected int level from Zabbix: %lu",
            hostdata.host.c_str(), hostdata.zbx_devdata.int_level);
   }

   if (0 != hostdata.zbx_devdata.devname.size()) hostdata.zbx_devdata.init = true;
   else hostdata.zbx_devdata.devname = config["zabbix"]["default-group"].get<conf::string_t>();

   if (hostdata.zbx_host.pingt_id != ping_templates[hostdata.zbx_devdata.ping_level])
   {
      for (unsigned int i = 0; i < ping_templates.size(); i++) {
         if (hostdata.zbx_host.pingt_id == ping_templates[i]) hostdata.zbx_devdata.ping_level = i; }
      logger.log_message(LOG_WARNING, funcname, "%s: host's parameters ping level doesn't match DB setting. "
                         "Overriding to actual level: %lu", hostdata.host.c_str(), hostdata.zbx_devdata.ping_level);
   }
}

int check_lowering(glob_hostdata &hostdata)
{
   static const char *funcname = "check_lowering";
   const conf::string_t &macroname = config["zabbix"]["lower-macro"].get<conf::string_t>();

   // Lower-macro is used to count how mant tries have been made with no response from host by snmp.
   // After three unsuccessful tries, we are considering that device is now of unknown type, even if it was known before.
   auto macro = hostdata.zbx_host.macros.find(macroname);
   if (hostdata.zbx_host.macros.end() == macro) hostdata.zbx_host.macros[macroname] = "1";
   else
   {
      switch(std::stoul(macro->second, nullptr, 10))
      {
         case 1: hostdata.zbx_host.macros[macroname] = "2"; break;
         case 2: hostdata.zbx_host.macros[macroname] = "3"; break;
         case 3: return 1; // Will proceed updating host as uknown device.
         default: logger.error_exit(funcname, "%s: unexpected value in lowering macro: %s", 
                       hostdata.host.c_str(), macro->second.c_str());
      }
   }

   buffer macros = build_host_macrostr(hostdata);
   zbx_sess.send_vstr(R"**(
      "method": "host.update",
      "params": {
         "hostid": "%lu",
         "macros": [ %s ] }
   )**", hostdata.zbx_host.id, macros.data());
   return 0;
}

void update_host_devtype(glob_hostdata &hostdata, buffer &clear_templates)
{
   uint_t temp_id;
   std::string from = hostdata.zbx_devdata.devname;
   std::string to = hostdata.db_devdata.devname;

   if (0 == from.size()) from = config["zabbix"]["default-group"].get<conf::string_t>();
   if (0 == to.size()) from = config["zabbix"]["default-group"].get<conf::string_t>();

   if (0 != (temp_id = zbx_api::get_groupid_byname(from, zbx_sess))) hostdata.zbx_host.groups.erase(temp_id);
   if (0 != (temp_id = zbx_api::get_templateid_byname(from, zbx_sess)))
   {
      clear_templates.append(R"**({"templateid": "%lu"},)**", temp_id);
      hostdata.zbx_host.templates.erase(temp_id);
   }

   if (0 != (temp_id = zbx_api::get_templateid_byname(to, zbx_sess))) hostdata.zbx_host.templates.insert(temp_id);
   if (0 == (temp_id = zbx_api::get_groupid_byname(to, zbx_sess))) temp_id = zbx_api::create_group(to, zbx_sess);
   hostdata.zbx_host.groups.insert(temp_id);
}

int check_supt_template(glob_hostdata &hostdata)
{
   uint_t temp_id;
   if (0 != hostdata.zbx_devdata.devname.size() and // not an unknown type
       0 != (temp_id = zbx_api::get_templateid_byname(hostdata.zbx_devdata.devname, zbx_sess)))
   {
      if (hostdata.zbx_host.templates.end() == hostdata.zbx_host.templates.find(temp_id))
      {
         hostdata.zbx_host.templates.insert(temp_id);
         return 1;
      }
   }
   return 0;
}

void zbx_update_host(glob_hostdata &hostdata)
{
   uint_t changes = 0;
   buffer clear_templates;

   parse_params_macro(hostdata);
   if (hostdata.db_devdata.devname != hostdata.zbx_devdata.devname)
   {
      // If device in zabbix has some actual 'device-type', but on last polling we couldn't get it,
      // we will make up to 3 tries, because device could have been unavailable due to some network failure.
      if (true == hostdata.zbx_devdata.init and
          false == hostdata.db_devdata.init and
          0 == check_lowering(hostdata)) return;
      update_host_devtype(hostdata, clear_templates);
      changes++;
   }
   else changes += check_supt_template(hostdata);

   buffer json;
   json.print(R"**("method": "host.update", "params": { "hostid": "%lu")**", hostdata.zbx_host.id);

   if (! hostdata.zbx_host.flags.test(dis_name_update))
      json.append(R"**(,"name": "%s%s")**", hostdata.db_devdata.prefix.c_str(), hostdata.name.c_str());

   if (! hostdata.zbx_host.flags.test(dis_ping_level_update) and 
       hostdata.zbx_devdata.ping_level != hostdata.db_devdata.ping_level)
   {
      hostdata.zbx_host.templates.insert(ping_templates[hostdata.db_devdata.ping_level]);
      hostdata.zbx_host.templates.erase(ping_templates[hostdata.zbx_devdata.ping_level]);
      clear_templates.append(R"**({"templateid": "%lu"},)**", ping_templates[hostdata.zbx_devdata.ping_level]);
      changes++;
   }

   if (! hostdata.zbx_host.flags.test(dis_int_level_update) and
       hostdata.zbx_devdata.int_level != hostdata.db_devdata.int_level)
   {
      uint_t template_id = config["zabbix"]["int-templateid"].get<conf::integer_t>();
      switch (hostdata.zbx_devdata.ping_level)
      {
         case 0: hostdata.zbx_host.templates.insert(template_id); break;
         case 1: hostdata.zbx_host.templates.erase(template_id);
                 clear_templates.append(R"**({"templateid": "%lu"},)**", template_id);
      }
      changes++;
   }

   hostdata.zbx_host.macros.erase(config["zabbix"]["lower-macro"].get<conf::string_t>());
   auto macro = hostdata.zbx_host.macros.find(config["zabbix"]["community-macro"].get<conf::string_t>());

   if (0 != hostdata.community.size() and 
         (hostdata.zbx_host.macros.end() == macro or
          (hostdata.zbx_host.macros.end() != macro and macro->second != hostdata.community)))
   {
      macro->second = hostdata.community;
      changes++;
   }

   /*
   if (0 == changes)
   {
      json.append('}');
      zbx_sess.send_vstr(json.data());
      return;
   }

   if (0 != templates_clear.size()) 
   {
      templates_clear.pop_back();
      json.append(R"**(,"templates_clear": [ %s ])**", templates_clear.data());
   }

   json.append(R"**(,"macros": [ %s ])**", build_host_macrostr(hostdata).data()); */


}
