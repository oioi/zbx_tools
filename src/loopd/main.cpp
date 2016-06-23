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
      { "update-interval",  { conf::val_type::integer } },
      { "poll-interval",    { conf::val_type::integer } },
      { "recheck-interval", { conf::val_type::integer } },

      { "bcmax",         { conf::val_type::integer } },
      { "mavlow",        { conf::val_type::integer } },
      { "mavmax",        { conf::val_type::integer } },
      { "recover-ratio", { conf::val_type::integer } }
   };

   conf::config_map notif_section {
      { "image-width" , { conf::val_type::integer } },
      { "image-height", { conf::val_type::integer } },
      { "from",         { conf::val_type::string  } },
      { "rcpts",        { conf::val_type::multistring } },
      { "smtphost",     { conf::val_type::string } }
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
devtasks action_data, action_queue, return_data;
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
   poller.clear();

   for (auto &device : devices)
   {
      if (hoststate::enabled != device.second.state) action_data.push_back(&(device.second));
      else poller.add(device.first.c_str(), device.second.community.c_str(),
               device.second.generic_req, callback, static_cast<void *>(&(device.second)));
   }

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
   const std::chrono::hours update_interval {config["poller"]["update-interval"].get<conf::integer_t>()};
   const std::chrono::seconds poll_interval {config["poller"]["poll-interval"].get<conf::integer_t>()};

   std::thread worker_thread;

   // Data to synchronize with updater thread.
   bool update_started {false};
   std::atomic<bool> updating {false};
   devsdata *newdata {};

   steady_clock::time_point begin, last_update {steady_clock::now()};
   std::unique_lock<std::mutex> datalock {syncdata.device_datalock, std::defer_lock};
   std::chrono::hours since_update;

   for (;;)
   {
      begin = steady_clock::now();
      datalock.lock();
      poller.poll();
      datalock.unlock();

      if (update_started and not updating)
      {
         logger.log_message(LOG_INFO, funcname, "device update finished. swapping data %lu -> %lu",
               newdata->size(), devices.size());
         update_started = false;

         prepare_data(devices, *newdata, worker_thread);
         delete newdata;
      }

      since_update = std::chrono::duration_cast<std::chrono::hours>(begin - last_update);      
      if (not updating and (devices.empty() or update_interval <= since_update))
      {
         datalock.lock();
         update_started = updating = true;
         newdata = new devsdata(devices);
         datalock.unlock();

         std::thread {update_devices, newdata, std::ref(updating)}.detach();
         last_update = steady_clock::now();
      }

      // Checking updates from worker thread.
      syncdata.updatelock.lock();
      if (syncdata.data_updated) 
      {
         for (auto it : return_data)
            poller.add(it->host.c_str(), it->community.c_str(),
                  it->generic_req, callback, static_cast<void *>(it));

         syncdata.data_updated = false;
         return_data.clear();
      }
      syncdata.updatelock.unlock();

      if (!action_queue.empty() or !alarm_queue.empty()) add_jobs();
      logger.log_message(LOG_INFO, funcname, "sleeping for %lds", 
            (poll_interval - std::chrono::duration_cast<std::chrono::seconds>(steady_clock::now() - begin)).count());
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

   catch (std::exception &exc) {
      logger.error_exit(progname, exc.what());
   }

   catch (...) {
      logger.error_exit(progname, "Aborted by generic catch. Something went really bad.");
   }

   return 0;
}
