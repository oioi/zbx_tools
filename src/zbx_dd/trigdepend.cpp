#include "aux_log.h"
#include "zbx_api.h"

#include "trigdepend.h"

uint_t get_template_unavail_trigid(uint_t template_id)
{
   static const char *funcname = "get_template_unavail_trigid";

   if (0 == zbx_sess.send_vstr(R"**(
      "method": "trigger.get",
      "params": {
         "output": [ "triggerid", "expression" ],
         "templateids": [ "%lu" ] }
   )**", template_id)) logger.error_exit(funcname, "Received no triggers for templateid: %lu", template_id);

   uint_t trigger_id = 0;
   buffer jsonpath, expr;

   for (int i = 0; ;i++)
   {
      jsonpath.print("result[%d].expression", i);
      if (false == zbx_sess.json_get_str(jsonpath.data(), &expr)) break;
      if (nullptr == strstr(expr.data(), "}=0")) continue;

      jsonpath.print("result[%d].triggerid", i);
      if (false == zbx_sess.json_get_uint(jsonpath.data(), &trigger_id))
         logger.error_exit(funcname, "Cannot get template %lu trigger id from JSON response", template_id);
      break;
   }

   if (0 == trigger_id)
      logger.error_exit(funcname, "There is not trigger host ICMP unavailability in template %lu", template_id);
   return trigger_id;
}

std::pair<uint_t, uint_t> get_host_unavail_trigid(uint_t host_id, uint_t template_id)
{
   static const char *funcname = "get_host_unavail_triggerid";

   if (0 == zbx_sess.send_vstr(R"**(
      "method": "trigger.get",
      "params": {
         "output": "triggerid",
         "hostids": [ "%lu" ],
         "filter": { "templateid": "%lu" },
         "selectDependencies": "" }
   )**", host_id, template_id)) logger.error_exit(funcname, "Received no trigger for"
      "host %lu ICMP unavailability with template ID %lu", host_id, template_id);

   std::pair<uint_t, uint_t> trigger(0, 0);

   if (false == zbx_sess.json_get_uint("result[0].triggerid", &(trigger.first)))
      logger.error_exit(funcname, "Cannot get host %lu trigger ID from JSON response", host_id);

   // NOTE: Can we have more than one dependency? Looks a bit dangerous.   
   zbx_sess.json_get_uint("result[0].dependencies[0].triggerid", &(trigger.second));
   return trigger;
}

void update_icmp_trigdepend(glob_hostdata &hostdata)
{
   static const char *funcname = "update_icmp_trigdepend";

   if (0 == zbx_sess.send_vstr(R"**(
      "method": "host.get",
      "params": {
         "output": [ "hostid" ],
         "selectParentTemplates": [ "templateid" ],
         "filter": { "host": [ "%s" ] } }
   )**", hostdata.uplink.c_str())) return;

   zabbix_hostdata zbx_uplink;
   if (false == zbx_sess.json_get_uint("result[0].hostid", &(zbx_uplink.id)))
      logger.error_exit(funcname, "%s: cannot get uplink '%s' host id from JSON response", 
            hostdata.host.c_str(), hostdata.uplink.c_str());

   buffer jsonpath;
   for (uint_t temp = 0, i = 0; ;i++)
   {
      jsonpath.print("result[0].parentTemplates[%lu].templateid", i);
      if (false == zbx_sess.json_get_uint(jsonpath.data(), &temp)) break;

      for (auto id : ping_templates) {
         if (temp == id) zbx_uplink.pingt_id = temp; }
   }

   if (0 == zbx_uplink.pingt_id)
   {
      logger.log_message(LOG_WARNING, funcname, "%s: host has no known ping template. "
            "Tried to make him uplink for: %s", hostdata.uplink.c_str(), hostdata.host.c_str());
      return;
   }

   // Selectting trigger IDs for ICMP unavailable in the templates.
   uint_t host_temp_trigid, uplink_temp_trigid;
   uplink_temp_trigid = get_template_unavail_trigid(zbx_uplink.pingt_id);

   // We already know both host and uplink ping templates, because we either created host or selected
   // his info from Zabbix. If both host and uplink have the same ping template, we don't need to
   // check it for other host.
   if (hostdata.zbx_host.pingt_id == zbx_uplink.pingt_id) host_temp_trigid = uplink_temp_trigid;
   else host_temp_trigid = get_template_unavail_trigid(hostdata.zbx_host.pingt_id);

   // Now we are using template's trigger IDs to select actual trigger IDs for concrete hosts.
   // Pair: first - trigger ID, second - dependency trigger ID.
   std::pair<uint_t, uint_t> host_trigid, uplink_trigid;
   host_trigid = get_host_unavail_trigid(hostdata.zbx_host.id, host_temp_trigid);
   uplink_trigid = get_host_unavail_trigid(zbx_uplink.id, uplink_temp_trigid);

   if (uplink_trigid.first == host_trigid.second) return;
   if (0 != host_trigid.second) zbx_sess.send_vstr(R"**(
      "method": "trigger.deleteDependencies", "params": [{"triggerid": "%u"}])**", host_trigid.first);

   zbx_sess.send_vstr(R"**(
      "method": "trigger.addDependencies",
      "params": {
         "triggerid": "%lu",
         "dependsOnTriggerid": "%lu" }
   )**", host_trigid.first, uplink_trigid.first);
}
