#ifndef LOOPD_DATA_H
#define LOOPD_DATA_H

#include <map>

#include "prog_config.h"
#include "device.h"

struct alarm_info
{
   device *dev;
   int_info *intf;

   alarm_info() : intf{nullptr} { }
   alarm_info(device *dev_, int_info *intf_) :
      dev{dev_}, intf{intf_} { }
};

using devtasks = std::vector<device *>;
using inttasks = std::vector<alarm_info>;

extern devsdata devices;

// These are accessed with locks (worker.h)
extern devtasks action_data, action_queue, return_data;
extern inttasks alarm_data, alarm_queue;

extern std::map<alarmtype, std::string> alarmtype_names;

int callback(int, snmp_session *, int, netsnmp_pdu *, void *, void *);
void prepare_request(device &);

#endif
