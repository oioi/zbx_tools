#ifndef LOOPD_DATA_H
#define LOOPD_DATA_H

#include <unordered_set>

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

struct msgdata
{
   const conf::string_t &from;
   const conf::multistring_t &rcpts;

   const device &dev;
   const int_info &intf;
   unsigned long bcrate;
};

using devtasks = std::vector<device *>;
using inttasks = std::vector<alarm_info>;

extern devsdata devices;
extern devtasks action_data, action_queue;
extern inttasks alarm_data, alarm_queue;

int callback(int, snmp_session *, int, netsnmp_pdu *, void *, void *);
void prepare_request(device &);

#endif
