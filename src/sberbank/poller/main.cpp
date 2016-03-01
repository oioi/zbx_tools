#include <boost/regex.hpp>
#include <string>
#include <unordered_map>

#include "MikrotikAPI.h"

#include "aux_log.h"
#include "prog_config.h"
#include "zbx_api.h"
#include "zbx_sender.h"

struct ext_point_data
{
   unsigned long active   {};
   unsigned long inactive {};
};

using ext_points = std::unordered_map<std::string, ext_point_data>;
using int_points = std::unordered_map<std::string, unsigned long>;

ext_points exts;
int_points ints;

const char *zbx_conffile {"/etc/zabbix/sberbank/zabbix-api.conf"};
const char *conffile {"/etc/zabbix/sberbank/sba-poll.conf"};
const char *progname {"sba-poll"};

conf::config_map zabbix {
   { "api-url",   { conf::val_type::string } },
   { "username",  { conf::val_type::string } },
   { "password",  { conf::val_type::string } }   
};

conf::config_map config {
   { "username", { conf::val_type::string } },
   { "password", { conf::val_type::string } },
};

boost::regex vlname {"[34]\\d{3}"};
boost::sregex_iterator rgx_end;

void parse_items(zbx_api::api_session &zbx_sess)
{
   buffer itemkey, jsonpath;
   std::string name;

   for (int i = 0; ;i++)
   {
      jsonpath.print("result[%d].key_", i);
      if (false == zbx_sess.json_get_str(jsonpath.data(), &itemkey)) break;

      name = itemkey.data();
      boost::sregex_iterator match {name.begin(), name.end(), vlname};

      if (rgx_end != match)
      {
         if ('3' == match->str()[0]) ints.insert(std::make_pair(match->str(), 0));
         else exts.insert(std::make_pair(match->str(), ext_point_data {}));
      }
   }
}

void get_poll_items(const std::string &hostname)
{
   static const char *funcname {"get_poll_items"};

   zbx_api::api_session zbx_sess;
   zbx_sess.set_auth(zabbix["api-url"].get<conf::string_t>(),
                     zabbix["username"].get<conf::string_t>(),
                     zabbix["password"].get<conf::string_t>());

   zbx_sess.send_vstr(R"**(
      "method": "host.get",
      "params": {
         "output": "hostid",
         "filter": { "ip": "%s" } }
   )**", hostname.c_str());

   uint_t hostid;
   if (false == zbx_sess.json_get_uint("result[0].hostid", &hostid))
      throw logging::error {funcname, "Failed to obtain hostid for host: %s", hostname.c_str()};

   zbx_sess.send_vstr(R"**(
      "method": "item.get",
      "params": {
         "hostids": "%lu",
         "output": [ "key_" ],
         "application": "Clients" }
   )**", hostid);

   parse_items(zbx_sess);
}

void parse_external(Block &block)
{
   wordmap words;
   ext_points::iterator it;

   for (int i = 0; i < block.Length(); i++)
   {
      words.clear();
      block[i].GetMap(words);
      if (0 == words.size()) continue;

      const std::string &server {words["server"]};
      boost::sregex_iterator match {server.begin(), server.end(), vlname};
      if (rgx_end == match) continue;

      if (exts.end() == (it = exts.find(match->str()))) continue;
      if ("true" == words["authorized"]) it->second.active++;
      else it->second.inactive++;
   }
}

void parse_internal(Block &block)
{
   wordmap words;
   int_points::iterator it;

   for (int i = 0; i < block.Length(); i++)
   {
      words.clear();
      block[i].GetMap(words);
      if (0 == words.size()) continue;

      const std::string &interface {words["interface"]};
      boost::sregex_iterator match {interface.begin(), interface.end(), vlname};
      if (rgx_end == match) continue;

      if (ints.end() == (it = ints.find(match->str()))) continue;
      it->second++;
   }
}

void poll_device(const std::string &hostname)
{
   MikrotikAPI api {
      hostname.c_str(),
      config["username"].get<conf::string_t>().c_str(),
      config["password"].get<conf::string_t>().c_str(), 8728
   };

   Sentence sentence;
   Block block;

   sentence.AddWord("/ip/hotspot/host/print");
   api.WriteSentence(sentence);
   api.ReadBlock(block);
   parse_external(block);

   sentence.Clear();
   sentence.AddWord("/ip/arp/print");
   api.WriteSentence(sentence);
   api.ReadBlock(block);
   parse_internal(block);
}

void send_to_zabbix(const std::string &hostname)
{
   static const char *funcname {"send_to_zabbix"};
   std::string key;
   zbx_sender zbxs;

   unsigned total_inactive {}, total_active {};

   for (const auto &en : exts)
   {
      key = "authorized[";
      key += en.first + ']';
      zbxs.add_data(hostname, key, en.second.active);
      total_active += en.second.active;

      key = "unauthorized[";
      key += en.first + ']';
      zbxs.add_data(hostname, key, en.second.inactive);
      total_inactive += en.second.inactive;
   }

   key = "authorized[total]";
   zbxs.add_data(hostname, key, total_active);
   key = "unauthorized[total]";
   zbxs.add_data(hostname, key, total_inactive);

   total_active = 0;
   for (const auto &en : ints)
   {
      key = "users[";
      key += en.first + ']';
      zbxs.add_data(hostname, key, en.second);
      total_active += en.second;
   }

   key = "users[total]";
   zbxs.add_data(hostname, key, total_active);

   sender_response result {zbxs.send()};
   logger.log_message(LOG_INFO, funcname, "Zabbix sender response: Processed: %u; Failed: %u; Total: %u; Spent: %f",
         result.processed, result.failed, result.total, result.elapsed);   
}

int main(int argc, char *argv[])
{
   openlog(progname, LOG_PID, LOG_LOCAL7);
   if (2 != argc) logger.error_exit(progname, "%s: wrong arg count: [ip]", argv[0]);

   try {
      if (0 == conf::read_config(conffile, config))
         logger.error_exit(progname, "Errors while reading configuration file.");

      if (0 == conf::read_config(zbx_conffile, zabbix))
         logger.error_exit(progname, "Errors while reading zabbix configuration file.");      

      std::string hostname {argv[1]};
      get_poll_items(hostname);
      poll_device(hostname);
      send_to_zabbix(hostname);
   }

   catch (logging::error &error) {
      logger.error_exit(progname, error.what());
   }

   catch (std::exception &exc) {
      logger.error_exit(progname, exc.what());
   }

   catch (...) {
      logger.error_exit(progname, "Aborted by generic catch. Something went really bad.");
   }

   return 0;
}
