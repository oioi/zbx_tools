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
   std::string hostname;
   std::string ip;

   ext_points exts;
   int_points ints;

   device_data(const std::string &hostname_, const std::string &ip_) : 
      hostname {hostname_}, ip{ip_} { }
};

using devsdata = std::vector<device_data>;

#endif
