#include <chrono>
#include <cmath>

#include "snmp/mux_poller.h"
#include "aux_log.h"
#include "prog_config.h"

#include "data.h"
#include "worker.h"

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
      { "update_interval",  { conf::val_type::integer } },
      { "poll_interval",    { conf::val_type::integer } },
      { "recheck_interval", { conf::val_type::integer } },

      { "mavsize",         { conf::val_type::integer } },
      { "bcmax",           { conf::val_type::integer } },
      { "mavmax",          { conf::val_type::integer } },
      { "recover_ratio",   { conf::val_type::integer } }
   };

   conf::config_map notif_section {
      { "image-width" ,  { conf::val_type::integer } },
      { "image-height",  { conf::val_type::integer } },
      { "from",          { conf::val_type::string  } },
      { "rcpts",         { conf::val_type::multistring } },
      { "smtphost",      { conf::val_type::string } }
   };

   conf::config_map snmp_section {
      { "default-community", { conf::val_type::string } }
   };
}

conf::config_map config {
   { "lockfile",  { conf::val_type::string, "/var/run/zabbix/loopd.pid" } },

   { "zabbix",    { conf::val_type::section, &zabbix_section } },
   { "snmp",      { conf::val_type::section, &snmp_section   } },
   { "poller",    { conf::val_type::section, &poller_section } },
   { "notifier",  { conf::val_type::section, &notif_section  } },

   { "datadir",   { conf::val_type::string      } }, 
   { "devgroups", { conf::val_type::multistring } },
};

devsdata devices;
devtasks action_data, action_queue;
inttasks alarm_data, alarm_queue;

snmp::mux_poller poller;
thread_sync syncdata;

void transfer_data(devsdata &maind, devsdata &repld)
{
   static const char *funcname {"transfer_data"};
   std::swap(maind, repld);

   std::vector<unsigned> intdel;
   std::vector<std::string> devdel;

   devsdata::iterator devit;
   intsdata::iterator intit;

   for (auto &device : maind)
   {
      if  (device.second.delmark)
      {
         for (auto &ints : device.second.ints) ints.second.rrdata.remove();
         remove(device.second.rrdpath.c_str());
         devdel.push_back(device.first);
         continue;
      }

      intdel.clear();
      for (auto &intf : device.second.ints)
      {
         if (intf.second.delmark)
         {
            intf.second.rrdata.remove();
            intdel.push_back(intf.first);
            continue;
         }

         if (repld.end() != (devit = repld.find(device.first)) and
             devit->second.ints.end() != (intit = devit->second.ints.find(intf.first))) 
            intf.second.data = intit->second.data;
      }

      for (auto n : intdel)
      {
         logger.log_message(LOG_INFO, funcname, "%s: deleted marked interface %u",
               device.first.c_str(), n);
         device.second.ints.erase(n);
      }

      if (hoststate::enabled == device.second.state) prepare_request(device.second);
   }

   for (auto n : devdel) 
   {
      logger.log_message(LOG_INFO, funcname, "%s: deleted marked device", n.c_str());
      maind.erase(n);
   }   
}

void rebuild_poller()
{
   poller.clear();
   for (auto &device : devices)
   {
      if (hoststate::enabled != device.second.state) continue;
      poller.add(device.first.c_str(), device.second.community.c_str(),
            device.second.generic_req, callback, static_cast<void *>(&(device.second)));
   }
}

void prepare_data(devsdata &maind, devsdata &newd, std::thread &worker_thread)
{
   // Waiting for worker thread to finish his current jobs and exit. Worker should complete all his alarm
   // notification jobs before exiting. All data has to be rebuilt because all pointers will be invalidated
   // after dataswap.
   if (worker_thread.joinable())
   {
      syncdata.statelock.lock();
      syncdata.running = false;

      if (syncdata.sleeping) { syncdata.sleeping = false; syncdata.wake.notify_all(); }
      syncdata.statelock.unlock();
      worker_thread.join();
   }

   transfer_data(maind, newd);
   action_data.clear();

   for (auto &device : maind) {
      if (hoststate::enabled != device.second.state) 
         action_data.push_back(&(device.second));
   }

   rebuild_poller();
   syncdata.running = true;
   worker_thread = std::thread{worker, &syncdata};
}

void add_jobs()
{
   syncdata.worker_datalock.lock();
   for (auto &entry : alarm_queue) alarm_data.push_back(entry);

   for (auto &entry : action_queue)
   {
      poller.erase(entry->host.c_str());
      action_data.push_back(entry);
   }

   alarm_queue.clear();
   action_queue.clear();
   syncdata.worker_datalock.unlock();

   syncdata.statelock.lock();
   if (syncdata.sleeping) { syncdata.sleeping = false; syncdata.wake.notify_all(); }
   syncdata.statelock.unlock();
}

void mainloop()
{
   static const char *funcname {"mainloop"};
   // So we're assuming that devices update interval should be measured in hours and
   // actual polling interval is measured in seconds (which better be at least a minute). 
   // No checks for unhealthy values, like zeroes or negatives.
   const std::chrono::hours update_interval {config["poller"]["update_interval"].get<conf::integer_t>()};
   const std::chrono::seconds poll_interval {config["poller"]["poll_interval"].get<conf::integer_t>()};

   std::thread worker_thread;

   // Data to synchronize with updater thread.
   bool update_started {false};
   std::atomic<bool> updating {false};
   devsdata *newdata {};

   steady_clock::time_point begin, last_update {steady_clock::now()};
   std::chrono::hours since_update;

   for (;;)
   {
      begin = steady_clock::now();
      poller.poll();

      if (update_started and !updating)
      {
         logger.log_message(LOG_INFO, funcname, "device update finished. swapping data %lu -> %lu",
               newdata->size(), devices.size());
         update_started = false;

         prepare_data(devices, *newdata, worker_thread);
         delete newdata;
      }

      since_update = std::chrono::duration_cast<std::chrono::hours>(begin - last_update);      
      if (0 == devices.size() or update_interval <= since_update)
      {
         std::lock_guard<std::mutex> lock {syncdata.device_datalock};
         update_started = updating = true;
         newdata = new devsdata(devices);

         std::thread {update_devices, newdata, &updating}.detach();
         last_update = steady_clock::now();
      }

      // NOTE: Check updates from worker thread.

      if (0 != action_queue.size() or 0 != alarm_queue.size()) add_jobs();
      std::this_thread::sleep_for(poll_interval - std::chrono::duration_cast<std::chrono::seconds>(steady_clock::now() - begin));
   }
}

int main()
{
   std::setlocale(LC_ALL, "en_US.UTF-8");
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
      logger.error_exit(progname, "Aborted by generic catch. Something went really bad.");
   }

   return 0;
}
