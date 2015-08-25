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
   max_objid_strlen = 128  // Should be enough, huh?
};

using flags_type = std::bitset<flags_bitwidth>;

enum class device_flags {
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

   zabbix_hostdata() : id(0), flags(0), pingt_id(0) { }
};

// Device parameters are filled from DB by device objID. 
struct device_params
{
   bool init;
   std::string name;
   std::string prefix;
   uint_t ping_level;
   uint_t int_level;

   device_params() : init(false), ping_level(1), int_level(0) { }
};

struct plain_hostdata
{
   std::string host;
   std::string name;

   buffer objid;
   std::string community;

   device_params params;

   plain_hostdata(const char *host_) : host(host_), name(host_) { }
};

extern buffer tempbuf;
extern zbx_api::api_session zbx_sess;
extern std::vector<uint_t> ping_templates;


#endif
