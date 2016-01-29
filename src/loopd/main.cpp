#include <chrono>
#include <locale>
#include <atomic>
#include <thread>
#include <limits>

#include "snmp/mux_poller.h"

#include "aux_log.h"
#include "prog_config.h"

#include "device.h"
#include "main.h"

using std::chrono::steady_clock;

namespace {
   const char *progname {"loopd"};
   const char *conffile {"loopd.conf"};

   conf::config_map zabbix_section {
      { "api-url",  { conf::val_type::string } },
      { "username", { conf::val_type::string } },
      { "password", { conf::val_type::string } }
   };

   conf::config_map poller_section {
      { "update_interval", { conf::val_type::integer } },
      { "poll_interval",   { conf::val_type::integer } },
      { "mavsize",         { conf::val_type::integer } }
   };

   conf::config_map snmp_section {
      { "default-community", { conf::val_type::string } }
   };

   std::vector<device *> action_queue;
}

conf::config_map config {
   { "lockfile",  { conf::val_type::string, "/var/run/zabbix/loopd.pid" } },

   { "zabbix",    { conf::val_type::section, &zabbix_section } },
   { "snmp",      { conf::val_type::section, &snmp_section   } },
   { "poller",    { conf::val_type::section, &poller_section } },

   { "datadir",   { conf::val_type::string      } }, 
   { "devgroups", { conf::val_type::multistring } },
};

void prepare_data(devsdata &maind, devsdata &newd)
{
   static const char *funcname {"prepare_data"};
   std::swap(maind, newd);

   std::vector<unsigned> intdel;   
   std::vector<std::string> devdel;

   const size_t ifbc_len = sizeof(snmp::oids::if_broadcast) / sizeof(oid);
   oid ifbc[ifbc_len];
   memcpy(ifbc, snmp::oids::if_broadcast, sizeof(snmp::oids::if_broadcast));   

   for (auto &device : maind)
   {
      if (device.second.flags & devflags::delete_mark)
      {
         for (auto &i : device.second.ints) i.second.rrdata.remove();
         remove(device.second.rrdpath.c_str());
         devdel.push_back(device.first);
         continue;
      }

      intdel.clear();
      device.second.generic_req = snmp_pdu_create(SNMP_MSG_GET);
      snmp_add_null_var(device.second.generic_req, snmp::oids::objid, sizeof(snmp::oids::objid) / sizeof(oid));
      snmp_add_null_var(device.second.generic_req, snmp::oids::tticks, sizeof(snmp::oids::tticks) / sizeof(oid));

      for (auto &i : device.second.ints)
      {
         if (i.second.deletemark)
         {
            i.second.rrdata.remove();
            intdel.push_back(i.first);
            continue;
         }

         // NOTE: Transfer actual interface data somewhere here?

         ifbc[ifbc_len - 1] = i.first;
         snmp_add_null_var(device.second.generic_req, ifbc, ifbc_len);
      }

      for (auto n : intdel)
      {
         logger.log_message(LOG_INFO, funcname, "%s: deleted marked interface %u",
               device.first.c_str(), n);
         device.second.ints.erase(n);
      }
   }

   for (auto n : devdel) 
   {
      logger.log_message(LOG_INFO, funcname, "%s: deleted marked device", n.c_str());
      maind.erase(n);
   }
}

void process_intdata(device *dev, netsnmp_variable_list *vars, double timedelta)
{
   static const char *funcname {"process_intdata"};
   static unsigned mavsize = config["poller"]["mavsize"].get<conf::integer_t>();

   uint64_t counter;
   intsdata::iterator it;

   for (unsigned i = 0; nullptr != vars; vars = vars->next_variable, i++)
   {
      if (ASN_COUNTER64 != vars->type)
         throw logging::error {funcname, "%s: unexpected ASN type in asnwer to ifbroadcast oid", dev->host.c_str()};

      if (dev->ints.end() == (it = dev->ints.find(*(vars->name + 11))))
         throw logging::error {funcname, "%s: host returned PDU with broadcast counter for unknown interface: %lu",
            dev->host.c_str(), *(vars->name + 11)};

      counter = vars->val.counter64->high << 32 | vars->val.counter64->low;      
      if (0 == it->second.counter)
      {
         it->second.counter = counter;
         continue;
      }

      // NOTE: I'm not sure this is correct. Still SNMPv2 states HC counters should be 64-bit.
      // So maybe we're calculating this right.
      double delta = (counter > it->second.counter) ? 
         (double) (counter - it->second.counter) / timedelta :
         (double) ((std::numeric_limits<uint64_t>::max() - it->second.counter) + counter) / timedelta;
      it->second.mav_vals.push_front(delta);

      // prevmav holds 'normal' MM level before alarm was triggered.
      if (false == it->second.alarmed) it->second.prevmav = it->second.lastmav;      

      size_t msize = it->second.mav_vals.size();


      


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
         dev->flags |= devflags::init;
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
      // NOTE: What to do with devices that don't respond any more? It can be a lot of them,
      // therefore we can't let ourselves to waste much time on timeouts. But we can't actually
      // just disable them, because device might be not responding due to actual network problem.
      // Additional thread to handle this type of devices?

   }

   return snmp::ok_close;
}

void mainloop()
{
   static const char *funcname {"mainloop"};
   // So we're assuming that devices update interval should be measured in hours and
   // actual polling interval is measured in seconds (which better be at least a minute). 
   // No checks for unhealthy values, like zeroes or negatives.
   const std::chrono::hours update_interval {config["poller"]["update_interval"].get<conf::integer_t>()};
   const std::chrono::seconds poll_interval {config["poller"]["poll_interval"].get<conf::integer_t>()};

   devsdata devices;
   bool update_started {false};
   std::atomic<bool> updating {false};
   devsdata *newdata {};   

   steady_clock::time_point begin, last_update {steady_clock::now()};
   std::chrono::hours since_update;
   snmp::mux_poller poller;

   for (;;)
   {
      begin = steady_clock::now();
      poller.poll();
      
      since_update = std::chrono::duration_cast<std::chrono::hours>(begin - last_update);
      if (update_started and !updating)
      {
         logger.log_message(LOG_INFO, funcname, "device update finished. swapping data %lu -> %lu",
               newdata->size(), devices.size());
         update_started = false;         
         prepare_data(devices, *newdata);
         delete newdata;

         poller.clear();
         for (auto &device : devices)
         {
            if (hoststate::enabled != device.second.state) continue;
            poller.add(device.first.c_str(), device.second.community.c_str(), 
                device.second.generic_req, callback, static_cast<void *>(&(device.second)));
         }
      }

      if (0 == devices.size() or update_interval <= since_update)
      {
         update_started = updating = true;
         newdata = new devsdata(devices);

         std::thread {update_devices, newdata, &updating}.detach();
         last_update = steady_clock::now();
      }

      std::cout << "Sleeping: " << (poll_interval - std::chrono::duration_cast<std::chrono::seconds>(steady_clock::now() - begin)).count() << std::endl;
      sleep((poll_interval - std::chrono::duration_cast<std::chrono::seconds>(steady_clock::now() - begin)).count());
   }
} 

int main(void)
{
   /*
   openlog(progname, LOG_PID, LOG_LOCAL7);
   daemonize(); 
   */

   std::setlocale(LC_ALL, "en_US.UTF-8");   std::unordered_map<std::string, device *> action_queue;
   logger.method = logging::log_method::M_STDE;

   try {
      if (0 == conf::read_config(conffile, config))
         logger.error_exit(progname, "Errors while reading configuration file.");

      init_snmp(progname);
      netsnmp_ds_set_int(NETSNMP_DS_LIBRARY_ID, NETSNMP_DS_LIB_OID_OUTPUT_FORMAT, NETSNMP_OID_OUTPUT_NUMERIC);

      mainloop();
   }

   catch (logging::error &error) {
      logger.error_exit(progname, error.what());
   }

   catch (...) {
      logger.error_exit(progname, "Aborted by generic exception. Something went really bad.");
   }

   return 0;
}
