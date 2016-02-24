#include <cstdint>

#include "snmp/mux_poller.h"
#include "prog_config.h"

#include "data.h"

void prepare_request(device &dev)
{
   static const size_t ifbc_len = sizeof(snmp::oids::if_broadcast) / sizeof(oid);
   static oid ifbc[ifbc_len] {};
   if (0 == ifbc[0]) memcpy(ifbc, snmp::oids::if_broadcast, sizeof(snmp::oids::if_broadcast));

   dev.generic_req = snmp_pdu_create(SNMP_MSG_GET);
   snmp_add_null_var(dev.generic_req, snmp::oids::objid, sizeof(snmp::oids::objid) / sizeof(oid));
   snmp_add_null_var(dev.generic_req, snmp::oids::tticks, sizeof(snmp::oids::tticks) / sizeof(oid));

   for (auto &intf : dev.ints)
   {
      ifbc[ifbc_len - 1] = intf.first;
      snmp_add_null_var(dev.generic_req, ifbc, ifbc_len);
   }
}

void check_alarm(int_info &it, device *dev)
{
   static const char *funcname {"check_alarm"};
   static const conf::integer_t bcmax {config["poller"]["bcmax"].get<conf::integer_t>()};
   static const conf::integer_t mavmax {config["poller"]["mavmax"].get<conf::integer_t>()};
   static const double recover_ratio {config["poller"]["recover_ratio"].get<conf::integer_t>() / 100.0};

   polldata &data = it.data;
   if (alarmtype::none != data.alarm)
   {
      double delta = data.mav_vals.front();
      if ((0 == data.prevmav and delta < bcmax) or delta < (data.prevmav * recover_ratio))
      {
         logger.log_message(LOG_INFO, funcname, "%s: alarm cleared on interface %s - %s",
               dev->host.c_str(), it.name.c_str(), it.alias.c_str());
         data.alarm = alarmtype::none;
         data.prevmav = 0;
         data.mav_vals.clear();
      }

      return;
   }

   size_t msize = data.mav_vals.size();
   unsigned ratio {};

   // NOTE: This should work as some kind of exponent based on maximum MM deque size.
   // Not like this at all.
   if (30 <= msize)     ratio = 10;
   else if (21 < msize) ratio = 8;
   else if (11 < msize) ratio = 5;
   else if (0 < msize)  ratio = 2;   

   // So, we're firing an alarm if:
   // A. Current raw broadcast pps on the interface is above bcmax constant.
   // B. Current moving average level is above mavmax constant.
   // C. We have some kind of spike on average data.
   if (bcmax < data.mav_vals.front()) data.alarm = alarmtype::bcmax;
   else if (mavmax < data.lastmav) data.alarm = alarmtype::mavmax;
   else if (0 != data.prevmav and (data.prevmav / ratio) < data.lastmav - data.prevmav) data.alarm = alarmtype::spike;

   if (alarmtype::none != data.alarm)
   {
      alarm_queue.emplace_back(dev, &it);
      logger.log_message(LOG_INFO, funcname, "%s: Detected abnormal broadcast pps level on interface %s - %s",
            dev->host.c_str(), it.name.c_str(), it.alias.c_str());
      logger.log_message(LOG_INFO, funcname, "%s: PMAV: %f; MAV: %f; Diff: %f; Ratio: %u; DMAV: %f",
            dev->host.c_str(), data.prevmav, data.lastmav, data.lastmav - data.prevmav, ratio, data.prevmav / ratio);
   }
}

void calculate_datamav(int_info &it)
{
   // Maximum size of MM deque. Bassicaly states what period is used for MM calculation.
   static const conf::integer_t mavsize {config["poller"]["mavsize"].get<conf::integer_t>()};

   polldata &data = it.data;
   if (alarmtype::none == data.alarm) data.prevmav = data.lastmav;
   int msize = data.mav_vals.size();

   if (mavsize < msize)
   {
      double delim = mavsize;
      data.lastmav = data.lastmav - (data.mav_vals.back() / delim) + (data.mav_vals.front() / delim);
      data.mav_vals.pop_back();
   }

   else
   {
      double sum {};
      for (auto &x : data.mav_vals) sum += x;
      data.lastmav = sum / msize;
   }

   it.rrdata.add_data(data.mav_vals.front(), data.lastmav);
}

void process_intdata(device *dev, netsnmp_variable_list *vars, double timedelta)
{
   static const char *funcname {"process_intdata"};
   static const uint64_t cmax {UINT64_MAX};

   intsdata::iterator it;
   uint64_t counter;

   for (unsigned i = 0; nullptr != vars; vars = vars->next_variable, ++i)
   {
      if (ASN_COUNTER64 != vars->type)
         throw logging::error {funcname, "%s: unexpected ASN type in answer to ifbroadcast oid", dev->host.c_str()};

      if (dev->ints.end() == (it = dev->ints.find(*(vars->name + 11))))
         throw logging::error {funcname, "%s: host returned PDU with broadcast counter for unknown interface: %lu",
            dev->host.c_str(), *(vars->name + 11)};

      counter = vars->val.counter64->high << 32 | vars->val.counter64->low;
      if (0 == it->second.data.counter)
      {
         it->second.data.counter = counter;
         continue;
      }

      double delta = (counter > it->second.data.counter) ?
         (double) (counter - it->second.data.counter) / timedelta :
         (double) ((cmax - it->second.data.counter) + counter) / timedelta;
      it->second.data.mav_vals.push_front(delta);

      calculate_datamav(it->second);
      check_alarm(it->second, dev);
   }
}

int callback(int operation, snmp_session *, int, netsnmp_pdu *pdu, void *magic, void *)
{
   static const char *funcname {"callback"};
   device *dev = static_cast<device *>(magic);

   if (NETSNMP_CALLBACK_OP_RECEIVED_MESSAGE == operation)
   {
      netsnmp_variable_list *vars = pdu->variables;
      std::string objid {snmp::print_objid(vars)};

      if (objid != dev->objid)
      {
         logger.log_message(LOG_INFO, funcname, "%s: device type has changed. "
               "PDU ignored. Device will be reinitialized.", dev->host.c_str());
         dev->state = hoststate::init;
         action_queue.push_back(dev);
         return snmp::ok_close;
      }

      vars = vars->next_variable;
      if (ASN_TIMETICKS != vars->type)
         throw logging::error {funcname, "%s: unexpected ASN type in answer to timeticks", dev->host.c_str()};

      int timedelta = (*(vars->val.integer) - dev->timeticks) / 100;
      dev->timeticks = *(vars->val.integer);
      process_intdata(dev, vars->next_variable, timedelta);
   }

   else
   {
      logger.log_message(LOG_INFO, funcname, "%s: device is unreachable.", dev->host.c_str());
      dev->state = hoststate::unreachable;
      action_queue.push_back(dev);
   }

   return snmp::ok_close;
}
