#include <boost/tokenizer.hpp>
#include <curl/curl.h>
#include <utility>

#include "MikrotikAPI.h"
#include "aux_log.h"
#include "prog_config.h"
#include "sqlite_db.h"
#include "zbx_sender.h"

#include "main.h"

namespace {
   conf::section_t db_section {
      { "hotspots-table", { conf::val_type::string, "hotspots" } },
      { "clients-table",  { conf::val_type::string, "clients" } }
   };

   const char *progname {"hotspotm"};

   time_t data_timestamp;
   hsdata hotspots;
   buffer sql_query;
   unsigned total_inactive {}, total_active {};
}

conf::config_map config {
   { "hostname",  { conf::val_type::string, "172.17.7.200" } },
   { "username",  { conf::val_type::string, "apiscript"    } },
   { "password",  { conf::val_type::string, "eHd8&6dt"     } },
   { "smtp-host", { conf::val_type::string, "smtp://192.168.133.100" } },
   { "db",        { conf::val_type::section, &db_section   } }
};

void get_hotspots()
{
   MikrotikAPI api = MikrotikAPI(
         config["hostname"].get<conf::string_t>().c_str(),
         config["username"].get<conf::string_t>().c_str(),
         config["password"].get<conf::string_t>().c_str(), 8728);

   Sentence sentence;
   sentence.AddWord("/ip/hotspot/print");
   api.WriteSentence(sentence);

   Block block;
   api.ReadBlock(block);

   wordmap words;
   for (int i = 0; i < block.Length(); i++)
   {
      words.clear();
      block[i].GetMap(words);

      if (0 == words.size()) continue;
      hotspots.insert(std::make_pair(words["name"], hotspot_data {}));
   }
}

void rebuild_hotspots()
{
   static const char *funcname {"rebuild_hotspots"};

   hotspots.clear();
   get_hotspots();
   logger.log_message(LOG_INFO, funcname, "Device reported %lu hotspots.", hotspots.size());

   data_timestamp = time(nullptr);
   const char *table = config["db"]["hotspots-table"].get<conf::string_t>().c_str();

   sql_query.append("drop table if exists %s;"
                    "create table %s(name text, stamp timestamp);",
         table, table);

   for (auto &hspot : hotspots)
      sql_query.append("insert into %s values('%s', %ld);",
         table, hspot.first.c_str(), data_timestamp);
}

extern "C" {
   int read_hotspots(void *, int, char **argv, char **)
   {
      // To make things easier, all timestamps for each hotspot should be equal.
      data_timestamp = std::strtoul(argv[1], nullptr, 10);
      hotspots.insert(std::make_pair(argv[0], hotspot_data {std::strtoul(argv[0], nullptr, 10)}));
      return 0;
   }

   int read_clients(void *, int, char **argv, char **)
   {
      hotspots[argv[1]].unique_clients.insert(std::strtoull(argv[0], nullptr, 10));
      return 0;
   }
}

void generate_report(time_t rawtime)
{
   static const char *funcname {"generate_report"};
   std::vector<std::string> rcpts {
      "v.petrov@westcall.spb.ru"
   };

   FILE *fp;
   if (nullptr == (fp = tmpfile())) throw logging::error(funcname, "Cannot open temporary file for e-mail message.");

   fprintf(fp, "From: hotspotsd@zabbix-iupd.v.westcall.net\r\n");
   for (const auto &to : rcpts) fprintf(fp, "To: %s\r\n", to.c_str());
   fprintf(fp, "Subject: Hotspots daily cients report\r\n"
               "Mime-Version: 1.0\r\n"
               "Content-Type: multipart/related; boundary=\"bound\"\r\n"
               "\r\n"
               "--bound\r\n"
               "Content-Type: text/html; charset=\"UTF-8\"\r\n\r\n"
               "Unique users per hotspot based on clinet's MAC-address in interval: %f mins\n"
               "<table>\n", (rawtime - data_timestamp) / 60.0);

   unsigned long total {};
   for (const auto &hspot : hotspots) 
   {
      fprintf(fp, "<tr><td>%s</td><td>%lu   </td></tr>\n", hspot.first.c_str(), hspot.second.unique_clients.size());
      total += hspot.second.unique_clients.size();
   }
   fprintf(fp, "<tr><td>Total</td><td>%lu</td></tr>\n</table>\n", total);

   CURL *curl;
   if (nullptr == (curl = curl_easy_init()))
      throw logging::error(funcname, "curl_easy_init() failed.");

   curl_easy_setopt(curl, CURLOPT_URL, config["smtp-host"].get<conf::string_t>().c_str());
   curl_easy_setopt(curl, CURLOPT_MAIL_FROM, "<hotspotsd@zabbix-iupd.v.westcall.net>");

   curl_slist *recipients {};
   for (const auto &to : rcpts) recipients = curl_slist_append(recipients, to.c_str());
   curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);

   rewind(fp);
   curl_easy_setopt(curl, CURLOPT_READDATA, fp);

   CURLcode res;
   if (CURLE_OK != (res = curl_easy_perform(curl)))
      throw logging::error(funcname, "curl_easy_perform() failed: %s", curl_easy_strerror(res));

   curl_slist_free_all(recipients);
   curl_easy_cleanup(curl);
}

void check_data(sqlite_db &db)
{
   static const char *funcname {"check_data"};
   buffer query;

   int handle = db.register_callback(read_hotspots);
   query.print("select * from %s", config["db"]["hotspots-table"].get<conf::string_t>().c_str());
   db.exec(query.data(), nullptr, handle);

   if (0 == hotspots.size()) 
   { 
      logger.log_message(LOG_INFO, funcname, "There is no data in hotspots DB. Rebuilding.");
      rebuild_hotspots(); 
      return; 
   }

   handle = db.register_callback(read_clients);
   query.print("select * from %s", config["db"]["clients-table"].get<conf::string_t>().c_str());
   db.exec(query.data(), nullptr, handle);
   logger.log_message(LOG_INFO, funcname, "Read %lu hotspots from DB", hotspots.size());   

   time_t rawtime = time(nullptr);
   tm *timeinfo = localtime(&rawtime);

   if (0 == timeinfo->tm_hour and (rawtime - data_timestamp) > 7200)
   {
      logger.log_message(LOG_INFO, funcname, "Reached new day (%02u:%02u). Building hotspots and report.",
            timeinfo->tm_hour, timeinfo->tm_min);
      generate_report(rawtime);
      rebuild_hotspots();
      return;
   }
}

uint64_t mac_to_int(const std::string &mac)
{
   uint64_t result {};
   uint8_t *ptr = reinterpret_cast<uint8_t *>(&result);
   boost::char_separator<char> sep {":"};

   boost::tokenizer<boost::char_separator<char>> tokens {mac, sep};
   for (const auto &tok : tokens)
   {
      *ptr = static_cast<uint8_t>(std::stoi(tok, nullptr, 16));
      ptr++;
   }
   return result;
}

void poll_router()
{
   MikrotikAPI api = MikrotikAPI(
         config["hostname"].get<conf::string_t>().c_str(),
         config["username"].get<conf::string_t>().c_str(),
         config["password"].get<conf::string_t>().c_str(), 8728);

   Sentence sentence;
   sentence.AddWord("/ip/hotspot/host/print");
   api.WriteSentence(sentence);

   Block block;
   api.ReadBlock(block);

   wordmap words;
   hsdata::iterator it;   

   for (int i = 0; i < block.Length(); i++)
   {
      words.clear();
      block[i].GetMap(words);

      if (0 == words.size()) continue;
      if (hotspots.end() == (it = hotspots.find(words["server"]))) continue;
      hotspot_data &data = it->second;

      if ("true" == words["authorized"]) 
      {
         data.active++; 
         total_active++; 
         data.unique_clients.insert(mac_to_int(words["mac-address"]));
      }

      else 
      { 
         data.inactive++; 
         total_inactive++; 
      }
   }
}

void send_to_zabbix()
{
   static const char *funcname {"send_to_zabbix"};
   const std::string &host {config["hostname"].get<conf::string_t>()};
   zbx_sender zbxs;
   std::string key;

   for (const auto &hspot : hotspots)
   {
      key = hspot.first;
      key += "[authorized]";
      zbxs.add_data(host, key, hspot.second.active);

      key = hspot.first;
      key += "[unauthorized]";
      zbxs.add_data(host, key, hspot.second.inactive);
   }

   zbxs.add_data(host, "total[authorized]", total_active);
   zbxs.add_data(host, "total[unauthorized]", total_inactive);
   sender_response result = zbxs.send();
   logger.log_message(LOG_INFO, funcname, "Zabbix sender response: Processed: %u; Failed: %u; Total: %u; Spent: %f",
         result.processed, result.failed, result.total, result.elapsed);
}

void save_clients()
{
   const char *table = config["db"]["clients-table"].get<conf::string_t>().c_str();

   sql_query.append("drop table if exists %s;"
                    "create table %s(cuid unsigned big int, hid int);",
         table, table);

   for (const auto &hspot : hotspots)
   {
      for (const auto &client : hspot.second.unique_clients)
         sql_query.append("insert into %s values(%lu, '%s');", table, client, hspot.first.c_str());
   }
}

int main(void)
{
   openlog(progname, LOG_PID, LOG_LOCAL7);

   try {
      sqlite_db db("hotspots.db");
      check_data(db);

      poll_router();
      send_to_zabbix();
      save_clients();

      if (0 != sql_query.size())
      {
         db.trans_exec(sql_query.data());
         db.exec("vacuum;");
      }
   }

   catch (logging::error &error) {
      logger.error_exit(progname, error.what());
   }

   return 0;
}
