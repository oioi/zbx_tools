#ifndef POLLER_MAIN_H
#define POLLER_MAIN_H

#include <unordered_map>
#include <vector>
#include <string>

struct ext_point_data
{
   unsigned long active   {};
   unsigned long inactive {};
};

using ext_points = std::unordered_map<std::string, ext_point_data>;
using int_points = std::unordered_map<std::string, unsigned long>;

struct device_data
{
   unsigned hostid;
   std::string hostname;
   std::string ip;

   ext_points exts;
   int_points ints;

   bool exts_total {false};
   bool ints_total {false};

   device_data(unsigned hostid_, const char *hostname_, const char *ip_) :
      hostid {hostid_}, hostname {hostname_}, ip {ip_} { }
};

using devsdata = std::vector<device_data>;

#endif
