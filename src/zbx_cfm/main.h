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
   uint_t zbx_id;

   cfm_alert_type alert_type;

   hostdata(const char *hostname_) : hostname(hostname_) { }
};


#endif

