#include <sstream>

#include "prog_config.h"
#include "aux_log.h"
#include "zbx_api.h"

#include "update.h"

buffer build_host_macrostr(const zabbix_hostdata &zbx_hostdata)
{
   buffer macros;
   for (auto &macro : zbx_hostdata.macros) 
      macros.append(R"**({"macro": "%s", "value": "%s"},)**", macro.first.c_str(), macro.second.c_str());
   macros.pop_back();
   return macros;
}

void parse_params_macro(const zabbix_hostdata &zbx_hostdata, device_params &params, const std::string &host)
{
   static const char *funcname = "parse_params_macro";

   auto macro = zbx_hostdata.macros.find(config["zabbix"]["param-macro"].get<conf::string_t>());
   if (zbx_hostdata.macros.end() == macro) logger.error_exit(funcname, "%s: host have no params macro. Possibly broken.", host.c_str());

   std::istringstream in(macro->second);
   std::string token;

   for (int i = 0; std::getline(in, token, ';'); i++)
   {
      switch (i)
      {
         case 0: params.ping_level = std::stoul(token, nullptr, 10); break;
         case 1: params.int_level = std::stoul(token, nullptr, 10); break;
         case 2: params.name = token; break;
         default: logger.error_exit(funcname, "%s: unexpected token count in params macro: %s", host.c_str(), macro->second.c_str());
      }
   }

   if (0 == params.ping_level or 3 < params.ping_level)
      logger.error_exit(funcname, "Got unexpected ping level from Zabbix for '%s' : %lu", host.c_str(), params.ping_level);
   if (0 != params.int_level and 1 != params.int_level)
      logger.error_exit(funcname, "Got unexpected int level from Zabbix for '%s' : %lu", host.c_str(), params.int_level);   

   if (0 != params.name.size()) params.init = true;
   else params.name = config["zabbix"]["default-group"].get<conf::string_t>();

   // If zabbix host's ping template doesn't match DB setting, then it was obviously changed manually.
   if (zbx_hostdata.pingt_id != ping_templates[params.ping_level])
   {
      for (unsigned int i = 0; i < ping_templates.size(); i++) {
         if (zbx_hostdata.pingt_id == ping_templates[i]) params.ping_level = i; }
      logger.log_message(LOG_WARNING, funcname, "%s: host's parameters ping level doesn't match DB setting. Overriding to to level: %lu",
            host.c_str(), params.ping_level);
   }
}

int check_lowering(const plain_hostdata &hostdata, zabbix_hostdata &zbx_hostdata)
{
   static const char *funcname = "check_lowering";
   const conf::string_t &macroname = config["zabbix"]["lower-macro"].get<conf::string_t>();

   // Lower-macro is used to count how many tries have been made with no response from host by snmp.
   // After three unsuccessful tries, we are considering that device is now of unknown type, even if it was known before.
   auto macro = zbx_hostdata.macros.find(macroname);
   if (zbx_hostdata.macros.end() == macro) zbx_hostdata.macros[macroname] = "1";
   else
   {
      uint_t tries = std::stoul(macro->second);
      switch (tries)
      {
         case 1: zbx_hostdata.macros[macroname] = "2"; break;
         case 2: zbx_hostdata.macros[macroname] = "3"; break;
         case 3: return 1; // Will proceed updating host with unknown type.
         default: logger.error_exit(funcname, "%s: unexpected value in lowering macro: %lu", hostdata.host.c_str(), tries);
      }
   }

   // Since we won't do anything with host, because we didn't reach necessary count of tries, just updating it's macro.
   buffer macros = build_host_macrostr(zbx_hostdata);
   zbx_sess.send_vstr(R"**(
      "method": "host.update",
      "params": {
         "hostid": "%lu",
         "macros": [ %s ] }
   )**", zbx_hostdata.id, macros.data());
   return 0;
}

int update_host_devspec(const plain_hostdata &hostdata, zabbix_hostdata &zbx_hostdata, buffer &clear_templates)
{
   std::string from = zbxhost_params.name;
   std::string to = hostdata.params.name;
   uint_t temp_id;
      
   if (0 == from.size()) from = config["zabbix"]["default-group"].get<conf::string_t>();
   if (0 == to.size()) to = config["zabbix"]["default-group"].get<conf::string_t>();

   if (0 != (temp_id = zbx_api::get_groupid_byname(from, zbx_sess))) zbx_hostdata.groups.erase(temp_id);
   if (0 != (temp_id = zbx_api::get_templateid_byname(from, zbx_sess)))
   {
      clear_templates.append(R"**({"templateid": "%lu"},)**", temp_id);
      zbx_hostdata.templates.erase(temp_id);
   }

   if (0 != (temp_id = zbx_api::get_templateid_byname(to, zbx_sess))) zbx_hostdata.templates.insert(temp_id);
   if (0 == (temp_id = zbx_api::get_groupid_byname(to, zbx_sess))) temp_id = zbx_api::create_group(to, zbx_sess);
   zbx_hostdata.groups.insert(temp_id);
}

int check_supt_template(const plain_hostdata &hostdata, zabbix_hostdata &zbx_hostdata)
{
   // Device type didn't change. Rechecking if any device template appeared.
   if (0 != hostdata.params.name.size() and
       0 != (temp_id = zbx_api::get_templateid_byname(hostdata.params.name, zbx_sess)))
   {
      if (zbx_hostdata.templates.end() == zbx_hostdata.templates.find(temp_id))
      {
         zbx_hostdata.templates.insert(temp_id);
         return 1;
      }
   }
   return 0;
}

void zbx_update_host(const plain_hostdata &hostdata, zabbix_hostdata &zbx_hostdata)
{
   uint_t temp_id, changes = 0;
   device_params zbxhost_params;
   buffer clear_templates, json;

   // Get device parameters from zabbix host. 
   // We will decide if any actions are required by comparing it to DB data.   
   parse_params_macro(zbx_hostdata, zbxhost_params, hostdata.host);
   json.print(R"**(
      "method": "host.update",
      "params" {
         "hostid": "%lu"
   )**", zbx_hostdata.id);

   if (zbxhost_params.name != hostdata.params.name)
   {
      // If device in zabbix has some actual 'device-type', but on last polling we couldn't get it,
      // we will make up to 3 tries, because device could have been unavaible because of some network failure.
      if (true == zbxhost_params.init and
          false == hostdata.params.init and
          0 == check_lowering(hostdata, zbx_hostdata)) return;
      update_host_despec(hostdata, zbx_hostdata, clear_templates);
      changes++;
   }
   else changes += check_supt_templates(hostdata, zbx_hostdata);

   if (! zbx_hostdata.flags.test(static_cast<int>(device_flags::dis_name_update)))
      json.append(R"**(,"name": "%s%s")**", hostdata.params.prefix.c_str(), hostdata.name.c_str());

   if (! zbx_hostdata.flags.test(static_cast<int>(device_flags::dis_ping_level_update))
       and zbxhost_params.ping_level != hostdata.params.ping_level)
   {
      zbx_hostdata.templates.insert(ping_templates[hostdata.params.ping_level]);
      zbx_hostdata.templates.erase(ping_templates[zbxhost_params.ping_level]);
      clear_templates.append(R"**({"templateid": "%lu"},)**", ping_templates[zbxhost_params.ping_level]);
      changes++;
   }

   if (! zbx_hostdata.flags.test(static_cast<int>(device_flags::dis_int_level_update))
       and zbxhost_params.int_level != hostdata.params.int_level)
   {
      uint_t template_id = config["zabbix"]["int-templateid"].get<conf::integer_t>();
      switch (zbxhost_params.int_level)
      {
         case 0: zbx_hostdata.templates.insert(template_id); break;
         case 1: zbx_hostdata.templates.erase(template_id);
                 clear_templates.append(R"**({"templateid": "%lu"},)**", template_id);
      }
      changes++;
   }

   auto macro = zbx_hostdata.macros.find(config["zabbix"]["community-macro"].get<conf::string_t>());
   if (0 != hostdata.community.size() and
         (zbx_hostdata.macros.end() == macro or
          (zbx_hostdata.macros.end() != macro and macro->second != hostdata.community)))
   {
      macro->second = hostdata.community;
      changes++;
   }







}
