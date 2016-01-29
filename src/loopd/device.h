#ifndef LOOPD_DEVICE_H
#define LOOPD_DEVICE_H

#include <string>
#include <unordered_map>
#include <atomic>
#include <deque>

#include "snmp/snmp.h"
#include "lrrd.h"

using flagstype = uint8_t;
enum class devflags : flagstype
{
   delete_mark = 1 << 0,  // Check if device still needs to be monitored after last device list update.
   init        = 1 << 1,  // Freshly added device - needs to be polled for additional data.
};

enum class hoststate
{
   enabled,     // Host is active and will be polled in each round
   unreachable, // Device didn't respond for some reason. Will retry for some time.
   disabled     // We're not trying anymore and host monitoring disabled till next device update round.
};

inline flagstype & operator |=(flagstype &val, devflags flag) {
   return val |= static_cast<flagstype>(flag); }

inline flagstype operator ~(devflags flag) { 
   return ~(static_cast<flagstype>(flag)); }

inline flagstype operator &(flagstype val, devflags flag) {
   return val & static_cast<flagstype>(flag); }

struct int_info
{
   std::string name;   
   std::string alias;

   bool deletemark {false};
   bool alarmed {false};
   rrd rrdata;

   uint64_t counter {};
   double lastmav;
   double prevmav;

   std::deque<double> mav_vals;
};

using intsdata = std::unordered_map<unsigned, int_info>;   
using intpair = std::pair<unsigned, int_info>;

struct device
{
   std::string host;
   std::string name;
   std::string objid;
   std::string community;
   std::string rrdpath;

   flagstype flags;
   hoststate state;

   intsdata ints;
   snmp::pdu_handle generic_req;

   time_t inttime {0};
   time_t timeticks {0};

   device(const std::string &host_, const std::string &name_, const std::string &community_, const std::string &rrdpath_) :
      host{host_}, name{name_}, community{community_}, rrdpath{rrdpath_}, flags{static_cast<flagstype>(devflags::init)} { }
};

using devsdata = std::unordered_map<std::string, device>;
using devpair = std::pair<const std::string, device>;

void update_devices(devsdata *, std::atomic<bool> *);

#endif
