#include "snmp/oids.h"

namespace snmp {
   namespace oids {
      const size_t tticks_size = 9;
      const oid tticks[] = { 1, 3, 6, 1, 2, 1, 1, 3, 0 };

      const size_t objid_size = 9;
      const oid objid[] = { 1, 3, 6, 1, 2, 1, 1, 2, 0 };

      const size_t iftype_size = 11;
      const oid iftype[] = { 1, 3, 6, 1, 2, 1, 2, 2, 1, 3, 0 };

      const size_t ifoperstatus_size = 11;
      const oid ifoperstatus[] = { 1, 3, 6, 1, 2, 1, 2, 2, 1, 8, 0 };

      const size_t ifname_size = 12;
      const oid ifname[] = { 1, 3, 6, 1, 2, 1, 31, 1, 1, 1, 1, 0 };

      const size_t ifalias_size = 12;
      const oid ifalias[] = { 1, 3, 6, 1, 2, 1, 31, 1, 1, 1, 18, 0 };

      const size_t ifhighspeed_size = 12;
      const oid ifhighspeed[] = { 1, 3, 6, 1, 2, 1, 31, 1, 1, 1, 15, 0 };

      const size_t ifbroadcast_size = 12;
      const oid ifbroadcast[] = { 1, 3, 6, 1, 2, 1, 31, 1, 1, 1, 9, 0 };
   }
}
