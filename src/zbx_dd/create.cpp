#include <sstream>

#include "buffer.h"
#include "prog_config.h"
#include "aux_log.h"

#include "create.h"

buffer build_host_macros(const glob_hostdata &hostdata)
{
   // Bitwidth of flag type theoretically can be changed, so writing plain nulls is not an option.
   flags_type blank;
   std::stringstream flag_str;
   flag_str << blank;

   buffer macros;
   macros.print(R"**({"macro": "%s", "value": "%s"}, {"macro": "%s", "value": "%lu;%lu;%s"})**", 
         config["zabbix"]["autod-macro"].get<conf::string_t>().c_str(), flag_str.str().c_str(),
         config["zabbix"]["param-macro"].get<conf::string_t>().c_str(),
         hostdata.db_devdata.ping_level, hostdata.db_devdata.int_level, hostdata.db_devdata.devname.c_str());   

   if (0 == hostdata.community.size()) return macros;
   macros.append(R"**(,{"macro": "%s", "value": "%s"})**",
         config["zabbix"]["community-macro"].get<conf::string_t>().c_str(), hostdata.community.c_str());
   return macros;
}

void zbx_create_host(glob_hostdata &hostdata)
{
   static const char *funcname = "zbx_create_host";
   uint_t group_id = 0, intt_id = 0, supt_id = 0;

   hostdata.zbx_host.pingt_id = ping_templates[hostdata.db_devdata.ping_level];
   switch (hostdata.db_devdata.int_level)
   {
      case interfaces_monitoring_off: break;
      case interfaces_monitoring_on: intt_id = config["zabbix"]["int-templateid"].get<conf::integer_t>(); break;
   }

   if (hostdata.db_devdata.init)
   {
      const std::string &devname = hostdata.db_devdata.devname;
      if (0 == (group_id = zbx_api::get_groupid_byname(devname, zbx_sess)))
         zbx_api::create_group(devname, zbx_sess);
      supt_id = zbx_api::get_templateid_byname(devname, zbx_sess);
   }
   else 
   {
      const conf::string_t &group = config["zabbix"]["default-group"].get<conf::string_t>();
      if (0 == (group_id = zbx_api::get_groupid_byname(group, zbx_sess)))
         logger.error_exit(funcname, "Can't get default group '%s' ID. Does it exist?", group.c_str());
   }

   buffer templates;
   templates.print(R"**({"templateid": "%lu"})**", hostdata.zbx_host.pingt_id);
   if (0 != intt_id) templates.append(R"**(,{"templateid": "%lu"})**", intt_id);
   if (0 != supt_id) templates.append(R"**(,{"templateid": "%lu"})**", supt_id);   
   buffer macros = build_host_macros(hostdata);

   zbx_sess.send_vstr(R"**(
      "method": "host.create",
      "params": {
         "host": "%s",
         "name": "%s%s",
         "interfaces": [{"type": 2, "main": 1, "useip": 1, "ip": "%s", "dns": "", "port": "161"}],
         "groups": [{"groupid": "%lu"}],
         "macros": [ %s ],
         "templates": [ %s ] }
   )**", hostdata.host.c_str(), hostdata.db_devdata.prefix.c_str(), hostdata.name.c_str(), 
         hostdata.host.c_str(), group_id, macros.data(), templates.data());

   if (false == zbx_sess.json_get_uint("result.hostids[0]", &(hostdata.zbx_host.id)))
      logger.error_exit(funcname, "Cannot get ID of just created host from JSON response.");   
}
