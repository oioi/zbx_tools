#include <cstdint>
#include <map>

#include "snmp/mux_poller.h"
#include "snmp/oids.h"
#include "prog_config.h"

#include "data.h"

std::map<alarmtype, std::string> alarmtype_names {
   { alarmtype::bcmax,  "raw broadcast max"    },
   { alarmtype::mavmax, "moving average max"   },
   { alarmtype::spike,  "spike on the average" }
};

void prepare_request(device &dev)
{
   static snmp::oid_handle ifbc {snmp::oids::ifbroadcast, snmp::oids::ifbroadcast_size};

   dev.generic_req = snmp_pdu_create(SNMP_MSG_GET);
   snmp_add_null_var(dev.generic_req, snmp::oids::objid, snmp::oids::objid_size);
   snmp_add_null_var(dev.generic_req, snmp::oids::tticks, snmp::oids::tticks_size);

   for (auto &intf : dev.ints)
   {
      ifbc[snmp::oids::ifbroadcast_size - 1] = intf.first;
      snmp_add_null_var(dev.generic_req, ifbc, snmp::oids::ifbroadcast_size);
   }
}

void check_alarm(int_info &it, device *dev, double mavsize)
{
   static const char *funcname {"check_alarm"};
   static const conf::integer_t bcmax  {config["poller"]["bcmax"].get<conf::integer_t>()};
   static const conf::integer_t mavmax {config["poller"]["mavmax"].get<conf::integer_t>()};
   static const conf::integer_t mavlow {config["poller"]["mavlow"].get<conf::integer_t>()};   
   static const double recover_ratio   {config["poller"]["recover-ratio"].get<conf::integer_t>() / 100.0};

   polldata &data = it.data;
   bool check_reset {false};

   if (alarmtype::none != data.alarm)
   {
      double delta = data.mav_vals.front();
      if ((alarmtype::bcmax  == data.alarm and delta < bcmax) or
          (alarmtype::mavmax == data.alarm and data.lastmav < mavmax) or
          (alarmtype::spike  == data.alarm and delta < (data.prevmav * recover_ratio)))
      {
         logger.log_message(LOG_INFO, funcname, "%s: alarm cleared on interface %s - %s",
               dev->host.c_str(), it.name.c_str(), it.alias.c_str());

         data.alarm = alarmtype::none;
         data.lastmav = delta;
         data.prevmav = 0;

         data.mav_vals.clear();
         check_reset = true;
      }

      else return;
   }

   // Because our current MV data can be filled quite differently, depeding on daemon uptime,
   // we're using different ratios. Starting from 80% and falling to 10% when we have data for an exactly hour.
   double ratio = 0.8 - 0.7 * (data.mav_vals.size() / mavsize);

   // So, we're firing an alarm if:
   // A. Current raw broadcast pps on the interface is above bcmax constant.
   // B. Current moving average level is above mavmax constant.
   // C. We have some kind of spike on average data and current level is above low threshold.
   if (bcmax < data.mav_vals.front()) data.alarm = alarmtype::bcmax;
   else if (mavmax < data.lastmav) data.alarm = alarmtype::mavmax;
   else if (0 != data.prevmav and mavlow < data.lastmav and (data.prevmav * ratio) < data.lastmav - data.prevmav) data.alarm = alarmtype::spike;

   if (alarmtype::none != data.alarm)
   {
      if (check_reset) 
      {
         logger.log_message(LOG_INFO, funcname, "%s: alarm on interface %s was cleared and reset to: %s", 
               dev->host.c_str(), it.name.c_str(), alarmtype_names[data.alarm].c_str());
         return;
      }

      alarm_queue.emplace_back(dev, &it);
      logger.log_message(LOG_INFO, funcname, "%s: Detected abnormal broadcast pps level on interface %s - %s (%s)",
            dev->host.c_str(), it.name.c_str(), it.alias.c_str(), alarmtype_names[data.alarm].c_str());
      logger.log_message(LOG_INFO, funcname, "%s: PMAV: %f; MAV: %f; Diff: %f; Ratio: %f; DMAV: %f",
            dev->host.c_str(), data.prevmav, data.lastmav, data.lastmav - data.prevmav, ratio, data.prevmav / ratio);
   }
}

void calculate_datamav(int_info &it, int mavsize)
{
   polldata &data = it.data;
   if (alarmtype::none == data.alarm) data.prevmav = data.lastmav;
   int msize = data.mav_vals.size();

   if (mavsize < msize)
   {
      data.lastmav = data.lastmav - (data.mav_vals.back() / mavsize) + (data.mav_vals.front() / mavsize);
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
   // We're using moving average for an hour period. 
   static const int mavsize {3600 / config["poller"]["poll-interval"].get<conf::integer_t>()};   

   static const uint64_t cmax {UINT64_MAX};
   static const double maxdelta {500000};

   intsdata::iterator it;
   uint64_t counter;
   double delta;

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

      if (counter < it->second.data.counter)
      {
         delta = (double) ((cmax - it->second.data.counter) + counter) / timedelta;

         if (delta > maxdelta) 
         {
            logger.log_message(LOG_INFO, funcname, "%s: %u counter resetted - skipped.",
               dev->host.c_str(), it->first);
            it->second.data.counter = counter;
            continue;
         }
      }
      else delta = (double) (counter - it->second.data.counter) / timedelta;

      it->second.data.mav_vals.push_front(delta);
      it->second.data.counter = counter;      

      calculate_datamav(it->second, mavsize);
      check_alarm(it->second, dev, mavsize);
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
