#ifndef ZBX_CFM_MAN_H
#define ZBX_CFM_MAN_H

#include <string>
#include <map>

#include "buffer.h"
#include "typedef.h"

enum { 
   max_objid_strlen = 1024      // Should be enough, huh?
};

enum class cfm_alert_type : int {
   zbx_sender = 1,
   snmp_trap
};

struct hostdata
{
   std::string hostname;
   buffer objid;

   uint_t host_id;
   uint_t interface_id;

   uint_t application_id;
   uint_t item_count;

   uint_t item_id;
   uint_t trigger_id;

   cfm_alert_type alert_type;
   std::string item_name;
   std::string trigger_expr;
   bool trap_item_exist;

   std::map<std::string, std::string> macros;

   hostdata(const char *hostname_) : hostname(hostname_), host_id(0), 
      interface_id(0), application_id(0), trap_item_exist(false) { }
};


#endif

