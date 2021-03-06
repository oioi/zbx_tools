#include <sstream>

#include "basic_mysql.h"
#include "aux_log.h"
#include "prog_config.h"

#include "mail.h"
#include "zbx_api.h"
#include "curl_cl.h"

#include "main.h"

namespace {
   conf::section_t zabbix_section {
      { "login-url",     { conf::val_type::string } },   // Used to login to web-interface.
      { "cookie-file",   { conf::val_type::string } },   // Cookie file to store cookies after logging.
      { "web-url",       { conf::val_type::string } },   // For images and href-s in the messages
      { "api-url",       { conf::val_type::string } },   // Obviously for API
      { "username",      { conf::val_type::string } },
      { "password",      { conf::val_type::string } }
   };

   conf::section_t ddstech_db {
      { "hostname", { conf::val_type::string } },
      { "username", { conf::val_type::string } },
      { "password", { conf::val_type::string } },
      { "port",     { conf::val_type::integer, 3306 } }
   };

   conf::section_t image_section {
      { "height",      { conf::val_type::integer,  150 } },
      { "width",       { conf::val_type::integer,  600 } }
   };

   const char *conffile = "/etc/zabbix/zbx_mail.conf";
   const char *progname = "zbx_mail";

   const std::vector<regexes> rgxs {
      { R"**(%GRAPH_ID_IMG\(\d+\)%)**",   insert_graphid_img,   14 },  // Insert image of existing graph (with graphid) selected by itemid.
      { R"**(%GRAPH_ITEM_IMG\(\d+\)%)**", insert_itemgraph_img, 16 },  // Insert image of graph (which is generated by item)

      { R"**(%BGP_PEERNAME\(\d+.\d+.\d+.\d+\)%)**", replace_peername, 14 } // Replace BGP peer IP with client name
   };   

   zbx_api::api_session zbx_sess;
   basic_curl::basic_http zbx_web;
   mail::mail_message message;
}

conf::section_t config {
   { "smtp-host",      { conf::val_type::string, "smtp://192.168.133.100"   } },
   { "smtp-from",      { conf::val_type::string, "<noreply@somezabbix.org>" } },
   { "smtp-from-name", { conf::val_type::string, "Zabbix " } },

   { "cache-dir",      { conf::val_type::string } },
   { "cache-timeout",  { conf::val_type::integer, 10 } },

   { "include-dir",    { conf::val_type::string } },
   { "zabbix",         { conf::val_type::section, &zabbix_section } },
   { "image",          { conf::val_type::section, &image_section } },
   { "ddstech-db",     { conf::val_type::section, &ddstech_db } }
};

void zbx_web_login(void)
{
   static bool logged_in = false;

   if (logged_in) return;

   zbx_web.send_data.print("name=%s&password=%s&enter=Sign in&autologin=1&request=",
         config["zabbix"]["username"].get<conf::string_t>().c_str(), config["zabbix"]["password"].get<conf::string_t>().c_str());
   zbx_web.cookie_login(config["zabbix"]["cookie-file"].get<conf::string_t>(), config["zabbix"]["login-url"].get<conf::string_t>());

   // Here should be a check, that we actually have logged in.

   logged_in = true;
}

void replace_peername(std::string &arg, std::string &line)
{
   static const char *errstr = "ERROR REPLACING BGPPEER MACRO; ERROR SECTON EID: ";
   basic_mysql db {config["ddstech-db"]["hostname"].get<conf::string_t>(),
                   config["ddstech-db"]["username"].get<conf::string_t>(),
                   config["ddstech-db"]["password"].get<conf::string_t>(),
                   config["ddstech-db"]["port"].get<conf::integer_t>()};

   if (0 == db.query(true, "select name from net_raw_data.zabbix_bgp where ip = '%s'", arg.c_str()))
   {
      message.add_error("Cannot find name for BGP Peer %s", arg.c_str());
      line = errstr;
      line += message.get_eid();
      return;
   }

   line = db.get(0, 0);
}

void insert_itemgraph_img(std::string &arg, std::string &line)
{
   static const char *errstr = "ERROR REPLACING (IMG <- GRAPH BY ITEM <- ITEM_ID) MACRO; ERROR SECTION EID: ";
   uint_t item_id;

   try { item_id = std::stoul(arg); }
   catch (...)
   {
      message.add_error("Incorrect argument in macro: %s", arg.c_str());
      line = errstr;
      line += message.get_eid();
      return;
   }

   zbx_web_login();
   buffer tempbuf;

   tempbuf.print("%s/chart.php?itemids[]=%lu&period=86400&width=%u&height=%u",
         config["zabbix"]["web-url"].get<conf::string_t>().c_str(), item_id,
         config["image"]["width"].get<conf::integer_t>(), config["image"]["height"].get<conf::integer_t>());
   zbx_web.get(tempbuf.data());

   tempbuf.print("img-itemid-%lu.png", item_id);
   message.add_image(tempbuf.data(), zbx_web.recv_data.data(), zbx_web.recv_data.size());

   tempbuf.print("<a href=\"%s/history.php?action=showgraph&itemids[]=%lu&period=86400\">"
                 "<img src=\"cid:img-itemid-%lu.png\" alt=\"Graph\"></a>",
                 config["zabbix"]["web-url"].get<conf::string_t>().c_str(), item_id, item_id);
   line = tempbuf.data();
}

void insert_graphid_img(std::string &arg, std::string &line)
{
   static const char *errstr = "ERROR REPLACING (IMG <- GRAPH_ID <- ITEM_ID) MACRO; ERROR SECTION EID: ";
   uint_t graph_id, item_id;

   try { item_id = std::stoul(arg); }
   catch (...)
   {
      message.add_error("Incorrect argument in macro: %s", arg.c_str());
      line = errstr;
      line += message.get_eid();
      return;
   }
   
   // NOTE: At this point i don't have an idea what to do if Zabbix returns more than one graph for item ID.
   // Theoretically this should not happen too often. So we're always taking first graph in result.
   // Maybe we should send all graphs if there are not too many of them.
   zbx_sess.send_vstr(R"**("method":"graph.get","params":{"output":"graphid","itemids":"%lu"})**", item_id);
   if (false == zbx_sess.json_get_uint("result[0].graphid", &graph_id))
   {
      message.add_error("No graph IDs in zabbix response for item ID: %lu", item_id);
      line = errstr;
      line += message.get_eid();
      return;
   }

   zbx_web_login();
   buffer tempbuf;

   // NOTE: Should be image checked too?
   tempbuf.print("%s/chart2.php?graphid=%lu&period=86400&width=%u&height=%u",
         config["zabbix"]["web-url"].get<conf::string_t>().c_str(), graph_id,
         config["image"]["width"].get<conf::integer_t>(), config["image"]["height"].get<conf::integer_t>());
   zbx_web.get(tempbuf.data());

   tempbuf.print("img-gid-%lu.png", graph_id);
   message.add_image(tempbuf.data(), zbx_web.recv_data.data(), zbx_web.recv_data.size());

   tempbuf.print("<a href=\"%s/charts.php?graphid=%lu\">"
                 "<img src=\"cid:img-gid-%lu.png\" alt=\"Graph\"></a>",
                 config["zabbix"]["web-url"].get<conf::string_t>().c_str(), graph_id, graph_id);
   line = tempbuf.data();
}

void process_text(const char *text)
{
   std::istringstream in(text);
   boost::sregex_iterator rgx_end;
   std::string repl_str, arg;
   int offset;

   for (std::string line; getline(in, line); message.add_line(line))
   {
      for (auto &regex : rgxs)
      {
         offset = 0;
         for (boost::sregex_iterator matches(line.begin(), line.end(), regex.expr);
              rgx_end != matches;
              ++matches)
         {
            arg = matches->str().substr(regex.arg_offset);
            arg.pop_back();
            arg.pop_back();

            repl_str.clear();
            regex.process(arg, repl_str);

            line.replace(matches->position() + offset, matches->length(), repl_str);
            offset += repl_str.size() - matches->length();
         }
      }
   }
}

void message_process(const char *body)
{
   if (0 == conf::read_config(conffile, config))
      throw logging::error(progname, "Errors in the configuration file.");

   message.set_from(config["smtp-from"].get<conf::string_t>());
   message.set_start();

   if (mail::cache_status::cache_msg_found == message.try_cache(body)) 
   {
      message.send(); 
      return; 
   }

   zbx_sess.set_auth(config["zabbix"]["api-url"].get<conf::string_t>(),
                     config["zabbix"]["username"].get<conf::string_t>(),
                     config["zabbix"]["password"].get<conf::string_t>());
   process_text(body);
   message.generate_message();
   message.send();
   if (mail::cache_status::cache_unaccessible != message.cache()) mail::cache_cleanup();   
}

int main(int argc, char *argv[])
{
   openlog(progname, LOG_PID, LOG_LOCAL7);
   if (4 != argc)
   {
      logger.log_message(LOG_CRIT, progname, "Wrong argument count: %d", argc);
      return 0;
   }

   curl_global_init(CURL_GLOBAL_ALL);
   message.set_to(argv[1]);
   message.set_subject(argv[2]); 

   try {
      message_process(argv[3]);
      curl_global_cleanup();
      return 0;
   }
   catch (logging::error &error) { logger.log_message(LOG_CRIT, progname, error.what()); }
   catch (...) { }

   logger.log_message(LOG_CRIT, progname, "Something bad happened: trying to send message without any processing.");
   try { message.send(argv[3]); }
   catch (logging::error &error) { logger.error_exit(progname, "Plain message send failed too: %s", error.what()); }

   curl_global_cleanup();
   return 0;
}
