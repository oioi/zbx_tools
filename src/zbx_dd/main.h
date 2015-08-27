#ifndef ZBX_DD_MAIN_H
#define ZBX_DD_MAIN_H

#include <map>
#include <set>
#include <string>
#include <bitset>
#include <vector>

#include "buffer.h"
#include "zbx_api.h"
#include "typedef.h"

enum { 
   flags_bitwidth = 8,  
   max_objid_strlen = 128,  // Should be enough, huh?
};

using flags_type = std::bitset<flags_bitwidth>;

enum int_levels {
   interfaces_monitoring_off = 0,
   interfaces_monitoring_on = 1
};

enum device_flags {
   dis_ping_depend_update = 0,
   dis_name_update,
   dis_ping_level_update,
   dis_int_level_update
};

struct zabbix_hostdata
{
   uint_t id;
   flags_type flags;
   uint_t pingt_id;

   std::map<std::string, std::string> macros;
   std::set<uint_t> templates;
   std::set<uint_t> groups;

   zabbix_hostdata() : id(0), pingt_id(0) { }
};

struct device_params
{
   bool init;                   // Is structure filled for a specific device
   std::string devname;         // Device type name
   std::string prefix;          // Device-specefic prefix. Added to visible host name in zabbix
   uint_t ping_level;
   uint_t int_level;

   device_params() : init(false), ping_level(1), int_level(0) { }
};

struct glob_hostdata
{
   std::string host;
   std::string name;

   buffer objid;
   std::string community;
   std::string uplink;

   device_params db_devdata;    // Fetched from DB by device objID
   device_params zbx_devdata;   // Current state in zabbix, stored as macro
   zabbix_hostdata zbx_host;    // zabbix specific device data

   glob_hostdata(const char *host_) : host(host_), name(host_) { }
};

extern zbx_api::api_session zbx_sess;
extern std::vector<uint_t> ping_templates;

#endif
