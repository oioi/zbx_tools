#ifndef ZBX_CFM_MAN_H
#define ZBX_CFM_MAN_H

#include "buffer.h"
#include "typedef.h"

enum { 
   max_objid_strlen = 1024      // Should be enough, huh?
};

struct hostdata
{
   std::string hostname;
   buffer objid;
   uint_t zbx_id;

   hostdata(const char *hostname_) : hostname(hostname_), objid(max_objid_strlen) { }
};


#endif

