#ifndef SBER_DISCOVERY_DATA_H
#define SBER_DISCOVERY_DATA_H

#include <string>
#include <vector>
#include <map>

// Basic struct used on stage of discovering and feeding data to Zabbix.
struct point_data
{
   unsigned id;
   std::string vlan_name;
   std::string phys_addr;

   point_data(unsigned id_, const char *vlan) : id{id_}, vlan_name{vlan} { }
};

using points = std::vector<point_data>;

struct addr_point_data
{
   unsigned int_id {};
   unsigned ext_id {};

   std::string int_vlan;
   std::string ext_vlan;

   unsigned long int_traffic_graphid {};
   unsigned long ext_traffic_graphid {};

   unsigned long int_users_graphid {};
   unsigned long ext_users_graphid {};

   std::string nameurl;
};

using ordered_points = std::map<std::string, addr_point_data>;

#endif
