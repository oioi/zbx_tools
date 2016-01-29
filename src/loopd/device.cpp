#include <chrono>
#include <locale>
#include <memory>
#include <set>

#include <sys/stat.h>
#include <sys/types.h>

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

void parse_zbxdata(devsdata &devices, zbx_api::api_session &zbx_sess)
{
   static const char *funcname {"parse_zbxdata"};
   static const conf::string_t &defcomm {config["snmp"]["default-community"].get<conf::string_t>()};
   static const conf::string_t &datadir {config["datadir"].get<conf::string_t>()};

   buffer jsonpath, result;
   std::string name, host, community;

   for (int i = 0; ;i++)
   {
      jsonpath.print("result[%d].host", i);
      if (false == zbx_sess.json_get_str(jsonpath.data(), &result)) break;
      host = result.data();

      jsonpath.print("result[%d].name", i);
      if (false == zbx_sess.json_get_str(jsonpath.data(), &result))
         throw logging::error {funcname, "%s: failed to get hostname.", host.c_str()};

      name = convertwc(zbx_api::parse_codestring(std::string {result.data()}));
      community.clear();      

      for (int j = 0; ;j++)
      {
         jsonpath.print("result[%d].macros[%d].macro", i, j);
         if (false == zbx_sess.json_get_str(jsonpath.data(), &result)) break;
         if (0 != strcmp(result.data(), "{$SNMP_COMMUNITY}")) continue;

         jsonpath.print("result[%d].macros[%d].value", i, j);
         if (false == zbx_sess.json_get_str(jsonpath.data(), &result))
            throw logging::error {funcname, "%s: failed to get macro's value.", host.c_str()};

         community = result.data();
         break;
      }

      if (0 == community.size()) community = defcomm;
      devsdata::iterator it = devices.find(host);

      if (devices.end() == it)
      {
         std::string devdir = datadir + '/' + host;
         // NOTE: Manage with the sutuation, when directory already exists? Handle EEXIST?
         if (-1 == mkdir(devdir.c_str(), S_IRWXU))
            throw logging::error {funcname, "failed to create device data directory '%s': %s",
               devdir.c_str(), strerror(errno)};

         logger.log_message(LOG_INFO, funcname, "Added new device %s: '%s' - %s",
               host.c_str(), name.c_str(), community.c_str());
         devices.emplace(std::piecewise_construct, std::forward_as_tuple(host), std::forward_as_tuple(host, name, community, devdir));         
      }

      else
      {
         if (it->second.name != name or it->second.community != community)
            logger.log_message(LOG_INFO, funcname, "Device updated %s: '%s' - %s",
                  host.c_str(), name.c_str(), community.c_str());

         it->second.flags &= ~devflags::delete_mark;
         it->second.name = name;
         it->second.community = community;         
      }
   }
}

void init_device(devpair &device)
{
   static const char *funcname {"init_device"};
   static const conf::string_t &defcom = config["snmp"]["default-community"].get<conf::string_t>();

   const char *host = device.first.c_str();
   snmp::sess_handle sessp;
   std::string objid;

   try 
   {
      sessp = snmp::init_snmp_session(host, device.second.community.c_str());
      objid = snmp::get_host_objid(sessp);
   }

   catch (snmp::snmprun_error &error)
   {
      if (device.second.community == defcom)
      {
         logger.log_message(LOG_INFO, funcname, "%s: device init failed: %s", host, error.what());
         device.second.state = hoststate::disabled;
         return;
      } 

      try
      {
         logger.log_message(LOG_INFO, funcname, "%s: retrying device with default SNMP community.", host);
         sessp = snmp::init_snmp_session(host, defcom.c_str());
         objid = snmp::get_host_objid(sessp);
      }

      catch (snmp::snmprun_error &error)
      {
         logger.log_message(LOG_INFO, funcname, "%s: device is not responding with any of known communities.", host);
         device.second.state = hoststate::disabled;
         return;
      }
   }

   if (0 != device.second.objid.size() and objid != device.second.objid)
   {
      logger.log_message(LOG_INFO, funcname, "%s: device type has changed from %s to %s",
            host, device.second.objid.c_str(), objid.c_str());
      // NOTE: Should we clear interfaces?
   }

   device.second.objid = objid;
   device.second.state = hoststate::enabled;
   logger.log_message(LOG_INFO, funcname, "%s: device initialized with type: %s", host, objid.c_str());
}

void update_ints(devpair &device)
{
   static const char *funcname {"update_ints"};
   snmp::intinfo info;

   try 
   {
      snmp::sess_handle sessp {snmp::init_snmp_session(device.first.c_str(), device.second.community.c_str())};
      snmp::intdata ints {snmp::get_host_physints(sessp)};
      info = snmp::get_intinfo(sessp, ints);
   }

   catch (snmp::snmprun_error &error)
   {
      logger.log_message(LOG_WARNING, funcname, "%s: device's interfaces update failed: %s",
            device.first.c_str(), error.what());
      device.second.state = hoststate::disabled;
   }   

   for (auto &i : device.second.ints) i.second.deletemark = true;
   buffer rrdpath;

   for (auto &inti : info)
   {
      if (false == inti.active) continue;
      int_info &it = device.second.ints[inti.id];
      it.alias = inti.alias;
      it.deletemark = false;

      if (0 != it.name.size())
      {
         logger.log_message(LOG_INFO, funcname, "%s: updated interface %u: %s - %s",
               device.first.c_str(), inti.id, inti.name.c_str(), inti.alias.c_str());
         continue;
      }

      logger.log_message(LOG_INFO, funcname, "%s: added interface %u: %s - %s",
            device.first.c_str(), inti.id, inti.name.c_str(), inti.alias.c_str());
      it.name = it.name;
      rrdpath.print("%s/%u.rrd", device.second.rrdpath.c_str(), inti.id);
      it.rrdata.init(rrdpath.data());
   }
}

void update_devices(devsdata *devices, std::atomic<bool> *updating)
{
   static const char *funcname {"update_devices"};

   zbx_api::api_session zbx_sess;
   zbx_sess.set_auth(config["zabbix"]["api-url"].get<conf::string_t>(),
                     config["zabbix"]["username"].get<conf::string_t>(),
                     config["zabbix"]["password"].get<conf::string_t>());

   std::chrono::steady_clock::time_point start {std::chrono::steady_clock::now()};
   const conf::multistring_t &groups {config["devgroups"].get<conf::multistring_t>()};
   std::vector<unsigned long> groupids;

   for (const auto &group : groups) groupids.push_back(zbx_api::get_groupid_byname(group, zbx_sess));
   for (auto &device : *devices) device.second.flags |= devflags::delete_mark;

   for (const auto &groupid : groupids)
   {
      zbx_sess.send_vstr(R"**(
         "method": "host.get",
         "params": {
            "groupids": "%lu",
            "output": [ "host", "name" ],
            "selectMacros": [ "macro", "value" ] }
      )**", groupid);
      parse_zbxdata(*devices, zbx_sess);       
   }

   unsigned deletemark {}, inactive {};
   for (auto &device : *devices)
   {
      if (device.second.flags & devflags::delete_mark) { deletemark++; continue; }
      if (device.second.flags & devflags::init) init_device(device);
      if (hoststate::disabled == device.second.state)  { inactive++; continue; }      
      update_ints(device);
   }

   // So we have updated datamap of devices with corresponding interfaces. Devices which were not received again from zabbix are
   // marked to be deleted. Same thing with interfaces. Since updating is performed in a separate thread, we're not
   // actually releasing any resources from here. Main thread will be signaled that data is updated. It will check any
   // resourced marked for deletion and release them appropriately. All new devices and interfaces are already initialized.
   *updating = false;
   std::chrono::duration<double> elapsed {std::chrono::steady_clock::now() - start};
   logger.log_message(LOG_INFO, funcname, "Updated devices in %fs. Total: %lu. Inactive: %u. Marked for deletion: %u",
         elapsed.count(), devices->size(), inactive, deletemark);
}

