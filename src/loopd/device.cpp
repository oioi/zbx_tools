#include <chrono>
#include <locale>
#include <memory>

#include <sys/stat.h>
#include <sys/types.h>

#include "snmp/snmp.h"
#include "aux_log.h"
#include "prog_config.h"
#include "zbx_api.h"

#include "device.h"

// And hope for the best.
// Current compiler doesn't support fancy codecvt and other stuff.
std::string convertwc(const std::wstring &from)
{
   static const size_t bufsize = 1024;
   std::unique_ptr<char []> out {new char[bufsize]};

   std::wcstombs(out.get(), from.c_str(), bufsize);
   return std::string {out.get()};
}

void escape_string(std::string &source)
{
   std::size_t pos = std::string::npos;
   while (std::string::npos != (pos = source.find('"', (pos == std::string::npos) ? 0 : pos + 2)))
      source.replace(pos, sizeof(char), "\\\"");
}

void create_device(devsdata &devices, const std::string &host, const std::string &name, std::string &community)
{
   static const char *funcname {"create_device"};
   static const conf::string_t &defcom {config["snmp"]["default-community"].get<conf::string_t>()};
   static const conf::string_t &datadir {config["datadir"].get<conf::string_t>()};

   if (community.empty()) community = defcom;
   devsdata::iterator it = devices.find(host);

   if (devices.end() != it)
   {
      if (it->second.name != name or it->second.community != community)
         logger.log_message(LOG_INFO, funcname, "Device updated %s: '%s' - %s",
               host.c_str(), name.c_str(), community.c_str());

      it->second.delmark = false;
      it->second.name = name;
      it->second.community = community;
      return;
   }

   std::string devdir {datadir + '/' + host};
   if (-1 == mkdir(devdir.c_str(), S_IRWXU))
   {
      // NOTE: Should we check directory permissions?
      if (EEXIST != errno) throw logging::error {funcname, "failed to create device data directory '%s': %s",
           devdir.c_str(), strerror(errno)};
   }

   logger.log_message(LOG_INFO, funcname, "Added new device %s: '%s' - %s",
         host.c_str(), name.c_str(), community.c_str());
   devices.emplace(std::piecewise_construct, std::forward_as_tuple(host), std::forward_as_tuple(host, name, community, devdir));
}


void parse_zbxdata(devsdata &devices, zbx_api::api_session &zbx_sess)
{
   static const char *funcname {"parse_zbxdata"};
   buffer jsonpath, result;
   std::string host, name, community;

   for (int i = 0; ;i++)
   {
      jsonpath.print("result[%d].name", i);
      if (false == zbx_sess.json_get_str(jsonpath.data(), &result)) break;

      name = convertwc(zbx_api::parse_codestring(std::string {result.data()}));
      community.clear();

      for (unsigned long type, j = 0; ;j++)
      {
         jsonpath.print("result[%d].interfaces[%lu].type", i, j);
         if (false == zbx_sess.json_get_uint(jsonpath.data(), &type)) break;
         if (2 != type) continue;

         jsonpath.print("result[%d].interfaces[%lu].ip", i, j);
         if (false == zbx_sess.json_get_str(jsonpath.data(), &result))
            throw logging::error {funcname, "%s: failed to get interface IP address.", name.c_str()};
         host = result.data();
         break;
      }

      for (int j = 0; ;j++)
      {
         jsonpath.print("result[%d].macros[%d].macro", i, j);
         if (false == zbx_sess.json_get_str(jsonpath.data(), &result)) break;
         if (0 != strcmp(result.data(), "{$SNMP_COMMUNITY}")) continue;

         jsonpath.print("result[%d].macros[%d].value", i, j);
         if (false == zbx_sess.json_get_str(jsonpath.data(), &result))
            throw logging::error {funcname, "%s: failed to get macro's value.", name.c_str()};

         community = result.data();
         break;
      }

      create_device(devices, host, name, community);
   }
}

void init_device(device &devdata)
{
   static const char *funcname {"init_device"};
   static const conf::string_t &defcom {config["snmp"]["default-community"].get<conf::string_t>()};

   const char *host = devdata.host.c_str();
   snmp::sess_handle sessp;
   std::string objid;

   try
   {
      sessp = snmp::init_snmp_session(host, devdata.community.c_str());
      objid = snmp::get_host_objid(sessp);
   }

   catch (snmp::snmprun_error &error)
   {
      if (devdata.community == defcom)
      {
         if (snmp::errtype::timeout != error.type) throw;
         logger.log_message(LOG_INFO, funcname, "%s: device init failed: %s", host, error.what());
         devdata.state = hoststate::unreachable;
         return;
      } 

      try
      {
         logger.log_message(LOG_INFO, funcname, "%s: retrying device with default SNMP community.", host);
         sessp = snmp::init_snmp_session(host, defcom.c_str());
         objid = snmp::get_host_objid(sessp);
         devdata.community = defcom;
      }

      catch (snmp::snmprun_error &error)
      {
         if (snmp::errtype::timeout != error.type) throw;
         logger.log_message(LOG_INFO, funcname, "%s: device is not responding with any of known communities.", host);
         devdata.state = hoststate::unreachable;
         return;
      }
   }

   if (!devdata.objid.empty() and objid != devdata.objid)
   {
      logger.log_message(LOG_INFO, funcname, "%s: device type has changed from %s to %s",
            host, devdata.objid.c_str(), objid.c_str());
      devdata.ints.clear();
   }

   devdata.objid = objid;
   devdata.state = hoststate::enabled;
   logger.log_message(LOG_INFO, funcname, "%s: device initialized with type: %s", host, objid.c_str());   
}

void update_ints(device &devdata)
{
   static const char *funcname {"update_ints"};
   static const conf::integer_t seconds {config["poller"]["poll-interval"].get<conf::integer_t>()};
   snmp::intinfo info;

   try
   {
      snmp::sess_handle sessp {snmp::init_snmp_session(devdata.host.c_str(), devdata.community.c_str())};
      snmp::intdata ints {snmp::get_host_physints(sessp)};
      info = snmp::get_intinfo(sessp, ints);
   }

   catch (snmp::snmprun_error &error)
   {
      if (snmp::errtype::timeout != error.type) throw;
      logger.log_message(LOG_WARNING, funcname, "%s: device's interfaces update failed: %s",
            devdata.host.c_str(), error.what());
      devdata.state = hoststate::unreachable;
   }

   buffer rrdpath;
   for (auto &intf : devdata.ints) intf.second.delmark = true;

   for (auto &inti : info)
   {
      if (false == inti.active) continue;
      int_info &it = devdata.ints[inti.id];

      it.id = inti.id;
      it.alias = inti.alias;
      it.delmark = false;

      if (!it.name.empty())
      {
         logger.log_message(LOG_INFO, funcname, "%s: updated interface %u: %s - %s",
               devdata.host.c_str(), inti.id, inti.name.c_str(), inti.alias.c_str());
         continue;
      }

      logger.log_message(LOG_INFO, funcname, "%s: added interface %u: %s - %s",
            devdata.host.c_str(), inti.id, inti.name.c_str(), inti.alias.c_str());
      it.name = inti.name;

      rrdpath.print("%s/%u.rrd", devdata.rrdpath.c_str(), inti.id);
      it.rrdata.init(rrdpath.data(), seconds);
   }
}

void update_devdata(devsdata *devices)
{
   static const char *funcname {"update_devdata"};
   zbx_api::api_session zbx_sess;
   zbx_sess.set_auth(config["zabbix"]["api-url"].get<conf::string_t>(),
                     config["zabbix"]["username"].get<conf::string_t>(),
                     config["zabbix"]["password"].get<conf::string_t>());

   std::chrono::steady_clock::time_point start {std::chrono::steady_clock::now()};
   const conf::multistring_t &groups = {config["devgroups"].get<conf::multistring_t>()};
   std::vector<unsigned long> groupids;

   for (const auto &group : groups) 
   {
      std::string name {group};
      escape_string(name);
      groupids.push_back(zbx_api::get_groupid_byname(name, zbx_sess));
   }
   for (auto &device : *devices) device.second.delmark = true;

   for (const auto &groupid : groupids)
   {
      zbx_sess.send_vstr(R"**(
         "method": "host.get",
         "params": {
            "groupids": "%lu",
            "output": [ "name" ],
            "selectMacros": [ "macro", "value" ],
            "selectInterfaces": [ "ip", "type" ] }
      )**", groupid);
      parse_zbxdata(*devices, zbx_sess);  
   }

   unsigned delmark {}, inactive {};
   for (auto &device : *devices)
   {
      if (device.second.delmark) { delmark++; continue; };
      if (hoststate::init == device.second.state) init_device(device.second);
      if (hoststate::enabled != device.second.state) { inactive++; continue; }
      update_ints(device.second);
   }

   // So we have updated datamap of devices with corresponding interfaces. Devices which were not received again from zabbix are
   // marked to be deleted. Same thing with interfaces. Since updating is performed in a separate thread, we're not
   // actually releasing any resources from here. Main thread will be signaled that data is updated. It will check any
   // resources marked for deletion and release them appropriately. All new devices and interfaces are already initialized.
   std::chrono::duration<double> elapsed {std::chrono::steady_clock::now() - start};
   logger.log_message(LOG_INFO, funcname, "Updated devices in %fs. Total: %lu. Inactive: %u. Marked for deletion: %u",
         elapsed.count(), devices->size(), inactive, delmark);   
}

void update_devices(devsdata *devices, std::atomic<bool> &updating)
{
   static const char *funcname {"update_devices"};

   try { update_devdata(devices); }

   catch (std::exception &exc) {
      logger.error_exit(funcname, "Exception thrown in updater thread: %s", exc.what());
   }

   catch (...) {
      logger.error_exit(funcname, "Updater thread aborted by generic catch clause.");
   }

   updating = false;
}
