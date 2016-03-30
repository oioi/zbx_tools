#include <boost/regex.hpp>
#include <fstream>

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

devsdata read_devices(const std::string &filename)
{
   std::ifstream in {filename};
   devsdata devices;

   std::string hostname, ip;
   while (in >> ip >> hostname)  devices.emplace_back(hostname, ip);

   return devices;
}

void parse_items(zbx_api::api_session &zbx_sess, device_data &dev)
{
   buffer itemkey, jsonpath;
   std::string name;

   boost::regex auth_clients {"^authorized\\[(\\d+)\\]$"};
   boost::regex free_clients {"^users\\[(\\d+)\\]$"};
   boost::smatch match;

   for (int i = 0; ; i++)
   {
      jsonpath.print("result[%d].key_", i);
      if (false == zbx_sess.json_get_str(jsonpath.data(), &itemkey)) break;

      name = itemkey.data();
      if (boost::regex_match(name, match, auth_clients)) 
         dev.exts.insert(std::make_pair(match[1], ext_point_data {}));

      if (boost::regex_match(name, match, free_clients))
         dev.ints.insert(std::make_pair(match[1], 0));
   }
}

void get_poll_items(devsdata &devices)
{
   static const char *funcname {"get_poll_items"};

   zbx_api::api_session zbx_sess;
   zbx_sess.set_auth(zabbix["api-url"].get<conf::string_t>(),
                     zabbix["username"].get<conf::string_t>(),
                     zabbix["password"].get<conf::string_t>());

   uint_t hostid;
   for (auto &dev : devices)
   {
      zbx_sess.send_vstr(R"**(
         "method": "host.get",
         "params": {
            "output": "hostid",
            "filter": { "host": "%s" } }
      )**", dev.hostname.c_str());

      if (false == zbx_sess.json_get_uint("result[0].hostid", &hostid))
         throw logging::error {funcname, "Failed to obtain hostid for host: %s", dev.hostname.c_str()};

      zbx_sess.send_vstr(R"**(
         "method": "item.get",
         "params": {
            "hostids": "%lu",
            "output": [ "key_" ],
            "application": "Clients" }
      )**", hostid);

      parse_items(zbx_sess, dev);
   }
}

void parse_external(Block &block, device_data &dev)
{
   ext_points::iterator it;
   wordmap words;

   boost::regex vlan {"\\d+"};
   boost::sregex_iterator rgx_end;

   for (int i = 0; i < block.Length(); i++)
   {
      words.clear();
      block[i].GetMap(words);
      if (0 == words.size()) continue;

      const std::string &server {words["server"]};
      boost::sregex_iterator match {server.begin(), server.end(), vlan};
      if (rgx_end == match) continue;

      if (dev.exts.end() == (it = dev.exts.find(match->str()))) continue;
      if ("true" == words["authorized"]) it->second.active++;
      else it->second.inactive++;
   }
}

void parse_internal(Block &block, device_data &dev)
{
   int_points::iterator it;
   wordmap words;

   boost::regex vlan {"\\d+"};
   boost::sregex_iterator rgx_end;

   for (int i = 0; i < block.Length(); i++)
   {
      words.clear();
      block[i].GetMap(words);
      if (0 == words.size()) continue;

      if ("false" == words["complete"]) continue;
      const std::string &interface {words["interface"]};
      boost::sregex_iterator match {interface.begin(), interface.end(), vlan};
      if (rgx_end == match) continue;

      if (dev.ints.end() == (it = dev.ints.find(match->str()))) continue;
      it->second++;
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

void send_to_zabbix(devsdata &devices)
{
   static const char *funcname {"send_to_zabbix"};

   zbx_sender zbxs;   
   std::string key;
   unsigned total_inactive {}, total_active {};

   for (const auto &dev : devices)
   {
      for (const auto &en : dev.exts)
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

      key = "authorized[total]";
      zbxs.add_data(dev.hostname, key, total_active);
      key = "unauthorized[total]";
      zbxs.add_data(dev.hostname, key, total_inactive);

      total_active = 0;
      for (const auto &en : dev.ints)
      {
         key = "users[";
         key += en.first + ']';
         zbxs.add_data(dev.hostname, key, en.second);
         total_active += en.second;
      }

      key = "users[total]";
      zbxs.add_data(dev.hostname, key, total_active);
   }

   sender_response result {zbxs.send()};
   logger.log_message(LOG_INFO, funcname, "Zabbix sender response: Processed: %u; Failed: %u; Total: %u; Spent: %f",
         result.processed, result.failed, result.total, result.elapsed);   
}


int main(int argc, char *argv[])
{
   openlog(progname, LOG_PID, LOG_LOCAL7);
   if (2 != argc) logger.error_exit(progname, "Wrong argumnets. Should be: %s [input_file]", argv[0]);

   try {
      if (0 == conf::read_config(conffile, config))
         logger.error_exit(progname, "Errors while reading configuration file.");

      if (0 == conf::read_config(zbx_conffile, zabbix))
         logger.error_exit(progname, "Errors while reading zabbix configuration file.");

      devsdata devices {read_devices(std::string {argv[1]})};
      get_poll_items(devices);
      poll_devices(devices);
      send_to_zabbix(devices);
   }

   catch (std::exception &e) {
      logger.error_exit(progname, e.what());
   }

   catch (...) {
      logger.error_exit(progname, "Aborted by generic catch.");
   }

   return 0;
}
