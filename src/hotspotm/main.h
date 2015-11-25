#ifndef HOTSPOTM_MAIN_H
#define HOTSPOTM_MAIN_H

#include <map>
#include <unordered_map>
#include <unordered_set>
#include <string>

using wordmap = std::map<std::string, std::string>;

struct hotspot_data
{
   hotspot_data(unsigned long id_ = 0) : id{id_} { }

   unsigned long id;
   unsigned long active {};
   unsigned long inactive {};
   std::unordered_set<uint64_t> unique_clients;
};

using hsdata = std::unordered_map<std::string, hotspot_data>;

#endif
