#ifndef LOOPD_DEVICE_H
#define LOOPD_DEVICE_H

#include <string>
#include <unordered_map>
#include <atomic>
#include <deque>

#include "snmp/snmp.h"
#include "lrrd.h"

enum class alarmtype
{
   none,
   bcmax,
   mavmax,
   spike
};

struct polldata
{
   alarmtype alarm {alarmtype::none};
   uint64_t counter {};
   double lastmav {};
   double prevmav {};
   std::deque<double> mav_vals;
};

struct int_info
{
   unsigned id;
   std::string name;
   std::string alias;

   bool delmark {false};
   rrd rrdata;
   polldata data;
};

using intsdata = std::unordered_map<unsigned, int_info>;
using intpair = std::pair<unsigned, int_info>;

enum class hoststate
{
   init,         // Freshly added device - needs to be polled for additional data.
   enabled,      // Host is active and will be polled on each round.
   unreachable,  // Device didn't respond for some reason. Will retry for some time.
   disabled      // Device have never actually reponsed for any request so far.
};

struct device
{
   std::string host;
   std::string name;
   std::string community;
   std::string rrdpath;
   std::string objid;

   hoststate state {hoststate::init};
   bool delmark {false};

   intsdata ints;
   snmp::pdu_handle generic_req;
   time_t timeticks {0};

   device(const std::string &host_, const std::string &name_, const std::string &community_, const std::string &rrdpath_) :
      host{host_}, name{name_}, community{community_}, rrdpath{rrdpath_} { }
};

using devsdata = std::unordered_map<std::string, device>;
using devpair = std::pair<const std::string, device>;

void update_devices(devsdata *, std::atomic<bool> *);

#endif
