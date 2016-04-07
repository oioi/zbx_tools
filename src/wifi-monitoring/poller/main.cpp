#include <boost/regex.hpp>

#include "aux_log.h"
#include "prog_config.h"
#include "zbx_api.h"
#include "zbx_sender.h"

#include "MikrotikAPI.h"
#include "main.h"

namespace {
   const char *zbx_conffile {"/etc/zabbix/wifimon/zabbix-api.conf"};
   const char *conffile {"/etc/zabbix/wifimon/poller.conf"};
   const char *progname {"wifi-poll"};

   conf::config_map zabbix {
      { "api-url",   { conf::val_type::string } },
      { "username",  { conf::val_type::string } },
      { "password",  { conf::val_type::string } }   
   };
}

conf::config_map config {
   { "username", { conf::val_type::string } },
   { "password", { conf::val_type::string } },
};

devsdata get_devices(zbx_api::api_session &zbx_sess)
{
   static const char *funcname {"get_devices"};

   uint_t groupid = zbx_api::get_groupid_byname("hotspots", zbx_sess);
   if (0 == groupid) throw logging::error {funcname, "Cannot obtain group ID for group 'hotspots'"};

   zbx_sess.send_vstr(R"**(
      "method": "host.get",
      "params": {
         "groupids": "%lu",
         "output": [ "host" ],
         "selectInterfaces": [ "ip" ] }
   )**", groupid);

   devsdata devices;
   buffer hostname, ip, jsonpath;
   uint_t hostid;

   for (int i = 0; ; i++)
   {
      jsonpath.print("result[%d].hostid", i);
      if (false == zbx_sess.json_get_uint(jsonpath.data(), &hostid)) break;

      jsonpath.print("result[%d].host", i);
      if (false == zbx_sess.json_get_str(jsonpath.data(), &hostname))
         throw logging::error {funcname, "cannot obtai hostname for device with hostid: %lu", hostid};

      jsonpath.print("result[%d].interfaces[0].ip", i);
      if (false == zbx_sess.json_get_str(jsonpath.data(), &ip))
         throw logging::error {funcname, "%s: cannot obtain IP from any interface.", hostname.data()};

      devices.emplace_back(hostid, hostname.data(), ip.data());
   }

   return devices;
}

void parse_items(zbx_api::api_session &zbx_sess, device_data &dev)
{
   buffer jsonpath, itemkey;
   std::string totalkey {"total"};
   std::string name;

   boost::regex auth_clients {"^authorized\\[(.*)\\]$"};
   boost::regex free_clients {"^users\\[(.*)\\]$"};   
   boost::smatch match;

   for (int i = 0; ;i++)
   {
      jsonpath.print("result[%d].key_", i);
      if (false == zbx_sess.json_get_str(jsonpath.data(), &itemkey)) break;
      name = itemkey.data();

      if (boost::regex_match(name, match, auth_clients)) 
      {
         if (totalkey == match[1]) dev.exts_total = true;
         else dev.exts.insert(std::make_pair(match[1], ext_point_data {}));
      }

      if (boost::regex_match(name, match, free_clients)) 
      {
         if (totalkey == match[1]) dev.ints_total = true;
         else dev.ints.insert(std::make_pair(match[1], 0)); 
      }
   }
}

devsdata prepare_data()
{
   zbx_api::api_session zbx_sess;
   zbx_sess.set_auth(zabbix["api-url"].get<conf::string_t>(),
                     zabbix["username"].get<conf::string_t>(),
                     zabbix["password"].get<conf::string_t>());

   devsdata devices {get_devices(zbx_sess)};
   for (auto &dev : devices)
   {
      zbx_sess.send_vstr(R"**(
         "method": "item.get",
         "params": {
            "hostids": "%lu",
            "output": [ "key_" ],
            "application": "Clients" }
      )**", dev.hostid);

      parse_items(zbx_sess, dev);
   }

   return devices;
}

void parse_external(Block &block, device_data &dev)
{
   ext_points::iterator it;
   wordmap words;

   for (int i = 0; i < block.Length(); i++)
   {
      words.clear();
      block[i].GetMap(words);
      if (0 == words.size()) continue;

      const std::string &server {words["server"]};
      for (auto &en : dev.exts)
      {
         if (std::string::npos == server.find(en.first)) continue;
         if ("true" == words["authorized"]) en.second.active++;
         else en.second.inactive++;
         break;
      }
   }
}

void parse_internal(Block &block, device_data &dev)
{
   int_points::iterator it;
   wordmap words;

   for (int i = 0; i < block.Length(); i++)
   {
      words.clear();
      block[i].GetMap(words);
      if (0 == words.size()) continue;

      if ("false" == words["complete"]) continue;
      const std::string &interface {words["interface"]};

      for (auto &en : dev.ints)
      {
         if (std::string::npos == interface.find(en.first)) continue;
         en.second++;
         break;
      }
   }
}

void poll_devices(devsdata &devices)
{
   const conf::string_t &username {config["username"].get<conf::string_t>()};
   const conf::string_t &password {config["password"].get<conf::string_t>()};

   Sentence sentence;
   Block block;

   for (auto &dev : devices)
   {
      MikrotikAPI api {dev.ip.c_str(), username.c_str(), password.c_str(), 8728};

      if (0 != dev.exts.size())
      {
         sentence.Clear();
         sentence.AddWord("/ip/hotspot/host/print");
         api.WriteSentence(sentence);
         api.ReadBlock(block);
         parse_external(block, dev);
      }

      if (0 != dev.ints.size())
      {
         sentence.Clear();
         sentence.AddWord("/ip/arp/print");
         api.WriteSentence(sentence);
         api.ReadBlock(block);
         parse_internal(block, dev);
      }      
   }
}

void send_to_zabbix(const devsdata &devices)
{
   static const char *funcname {"send_to_zabbix"};

   zbx_sender zbxs;   
   std::string key;
   unsigned total_inactive, total_active;

   for (auto &dev : devices)
   {
      total_inactive = total_active = 0;

      for (auto &en : dev.exts)
      {
         key = "authorized[";
         key += en.first + ']';
         zbxs.add_data(dev.hostname, key, en.second.active);
         total_active += en.second.active;

         key = "unauthorized[";
         key += en.first + ']';
         zbxs.add_data(dev.hostname, key, en.second.inactive);
         total_inactive += en.second.inactive;
      }

      if (dev.exts_total)
      {
         key = "authorized[total]";
         zbxs.add_data(dev.hostname, key, total_active);
         key = "unauthorized[total]";
         zbxs.add_data(dev.hostname, key, total_inactive);
      }

      total_active = 0;
      for (auto &en : dev.ints)
      {
         key = "users[";
         key += en.first + ']';
         zbxs.add_data(dev.hostname, key, en.second);
         total_active += en.second;
      }

      if (dev.ints_total)
      {
         key = "users[total]";
         zbxs.add_data(dev.hostname, key, total_active);
      }
   }

   sender_response result {zbxs.send()};
   logger.log_message(LOG_INFO, funcname, "Zabbix sender response: Processed: %u; Failed: %u; Total: %u; Spent: %f",
         result.processed, result.failed, result.total, result.elapsed);   
}

int main(void)
{
   openlog(progname, LOG_PID, LOG_LOCAL7);

   try {
      if (0 == conf::read_config(conffile, config))
         logger.error_exit(progname, "Errors while reading configuration file.");

      if (0 == conf::read_config(zbx_conffile, zabbix))
         logger.error_exit(progname, "Errors while reading zabbix configuration file.");

      devsdata devices {prepare_data()};
      poll_devices(devices);
      send_to_zabbix(devices);
   }

   catch (std::exception &exc) {
      logger.error_exit(progname, exc.what());
   }

   catch (...) {
      logger.error_exit(progname, "Aborted by generic catch.");
   }

   return 0;
}
