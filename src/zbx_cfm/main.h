#ifndef ZBX_CFM_MAN_H
#define ZBX_CFM_MAN_H

#include "buffer.h"
#include "typedef.h"

enum { 
   max_objid_strlen = 1024      // Should be enough, huh?
};

enum class cfm_alert_type : int {
   ZBX_SENDER = 1,
   SNMP_TRAP
};

struct hostdata
{
   std::string hostname;
   buffer objid;

   uint_t host_id;
   uint_t interface_id;

   uint_t application_id;
   uint_t item_count;

   cfm_alert_type alert_type;
   std::string item_name;
   std::string trigger_expr;
   bool trap_item_exist;

   hostdata(const char *hostname_) : hostname(hostname_), host_id(0), 
      interface_id(0), application_id(0), trap_item_exist(false) { }
};


#endif

