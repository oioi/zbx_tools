#ifndef SNMPLIB_OIDS_H
#define SNMPLIB_OIDS_H

#include "snmp/snmp.h"

namespace snmp {
   namespace oids {

      extern const size_t objid_size;
      extern const oid objid[];

      extern const size_t iftype_size;
      extern const oid iftype[];

      extern const size_t ifoperstatus_size;
      extern const oid ifoperstatus[];

      extern const size_t ifname_size;
      extern const oid ifname[];

      extern const size_t ifalias_size;
      extern const oid ifalias[];

      extern const size_t ifhighspeed_size;
      extern const oid ifhighspeed[];
   }
}

#endif
