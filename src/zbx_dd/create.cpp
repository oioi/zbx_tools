#include <sstream>

#include "buffer.h"
#include "prog_config.h"
#include "aux_log.h"

#include "create.h"

buffer build_host_macro(const plain_hostdata &hostdata)
{
   // Bitwidth of flag type theoretically can be changed, so writing plain nulls is not an option.
   flags_type blank;
   std::stringstream flag_str;
   flag_str << blank;

   buffer macros;
   macros.print(R"**({"macro": "%s", "value": "%s"}, {"macro": "%s", "value": "%lu;%lu;%s"})**", 
         config["zabbix"]["autod-macro"].get<conf::string_t>().c_str(), flag_str.str().c_str(),
         config["zabbix"]["param-macro"].get<conf::string_t>().c_str(),
         hostdata.params.ping_level, hostdata.params.int_level, hostdata.params.name.c_str());

   if (0 == hostdata.community.size()) return macros;
   macros.append(R"**(,{"macro": "%s", "value": "%s"})**",
         config["zabbix"]["community-macro"].get<conf::string_t>().c_str(), hostdata.community.c_str());
   return macros;
}

void zbx_create_host(const plain_hostdata &hostdata, zabbix_hostdata zbx_hostdata)
{
   static const char *funcname = "zbx_create_host";
   uint_t group_id = 0, intt_id = 0, supt_id = 0;

   if (0 == hostdata.params.ping_level or 3 < hostdata.params.ping_level)
      logger.error_exit(funcname, "Unexpected ping level (%lu) for host %s.", 
            hostdata.params.ping_level, hostdata.host.c_str());
   zbx_hostdata.pingt_id = ping_templates[hostdata.params.ping_level];

   switch (hostdata.params.int_level)
   {
      case 0: break;
      case 1: intt_id = config["zabbix"]["int-templateid"].get<conf::integer_t>(); break;
      default: logger.error_exit(funcname, "Unexpected interface level (%lu) for host %s.",
                                 hostdata.params.int_level, hostdata.host.c_str());
   }

   if (hostdata.params.init)
   {
      if (0 == (group_id = zbx_api::get_groupid_byname(hostdata.params.name, zbx_sess))) 
         zbx_api::create_group(hostdata.params.name, zbx_sess);
      supt_id = zbx_api::get_templateid_byname(hostdata.params.name, zbx_sess);
   }
   else group_id = zbx_api::get_groupid_byname(config["zabbix"]["default-group"].get<conf::string_t>(), zbx_sess);

   buffer templates;
   templates.print(R"**({"templateid": "%lu"})**", zbx_hostdata.pingt_id);
   if (0 != intt_id) templates.append(R"**(,{"templateid": "%lu"})**", intt_id);
   if (0 != supt_id) templates.append(R"**(,{"templateid": "%lu"})**", supt_id);
   buffer macros = build_host_macro(hostdata);

   zbx_sess.send_vstr(R"**(
      "method": "host.create",
      "params": {
         "host": "%s",
         "name": "%s%s",
         "interfaces": [{"type": 2, "main": 1, "useip": 1, "ip": "%s", "dns": "", "port": "161"}],
         "groups": [{"groupid": "%lu"}],
         "macros": [ %s ],
         "templates": [ %s ] }
   )**", hostdata.host.c_str(), hostdata.params.prefix.c_str(), hostdata.name.c_str(), 
         hostdata.host.c_str(), group_id, macros.data(), templates.data());

   if (false == zbx_sess.json_get_uint("result.hostids[0]", &(zbx_hostdata.id)))
      logger.error_exit(funcname, "Cannot get ID of just created host from JSON response.");
}
