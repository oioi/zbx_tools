#include <cstdio>
#include <fcntl.h>
#include <curl/curl.h>

#include <openssl/bio.h>
#include <openssl/evp.h>

#include "aux_log.h"
#include "worker.h"
#include "data.h"

unsigned long check_bc_rate(const device *dev, unsigned intnum)
{
   static const char *funcname {"check_bc_rate"};
   static const std::chrono::seconds interval{config["poller"]["recheck_interval"].get<conf::integer_t>()};

   netsnmp_pdu *req;
   snmp::pdu_handle response;
   uint64_t counter {}, delta {};

   snmp::sess_handle sessp {snmp::init_snmp_session(dev->host.c_str(), dev->community.c_str())};   
   for (unsigned i = 0; i < 2; i++)
   {
      req = snmp_pdu_create(SNMP_MSG_GET);
      snmp_add_null_var(req, snmp::oids::if_broadcast, sizeof(snmp::oids::if_broadcast) / sizeof(oid));
      req->variables->name[(sizeof(snmp::oids::if_broadcast) / sizeof(oid)) - 1] = intnum;

      try { response = snmp::synch_request(sessp, req); }
      catch (snmp::snmprun_error &error)
      {
         if (snmp::errtype::timeout == error.type) return 0;
         else throw;
      }

      if (ASN_COUNTER64 != response.pdu->variables->type)
         throw logging::error {funcname, "%s: Unexpected ASN type in asnwer", dev->host.c_str()};

      counter = response.pdu->variables->val.counter64->high << 32 | response.pdu->variables->val.counter64->low;
      if (0 == i) { delta = counter; std::this_thread::sleep_for(interval); }
   }

   delta = (counter - delta) / interval.count();
   return (unsigned long) delta;
}

FILE * generate_message(const char *filename, const msgdata &data)
{
   static const char *funcname {"generate_message"};

   int fd = open(filename, O_RDONLY);
   if (-1 == fd) throw logging::error {funcname, "Failed top open graph file '%s': %s.",
      filename, strerror(errno)};

   FILE *fp = tmpfile();
   if (nullptr == fp) throw logging::error {funcname, "Failed to create temporary datafile."};

   fprintf(fp, "From: %s\r\n", data.from.c_str());
   for (const auto &to : data.rcpts) fprintf(fp, "To: %s\r\n", to.c_str());

   fprintf(fp, "Subject: %s: High broadcast pps level - %s\r\n"
               "Mime-Version: 1.0\r\n"
               "Content-Type: multipart/related; boundary=\"bound\"\r\n"
               "\r\n"
               "--bound\r\n"
               "Content-Type: text/html; charset=\"UTF-8\"\r\n\r\n",
         data.dev.host.c_str(), data.intf.name.c_str());

   fprintf(fp, "High broadcast pps level detected on device: %s - %s<br>\n"
               "Interface: %s - %s<br>\n"
               "Broadcast pps measured in last 2 seconds: %lu<br><br>\n",
               data.dev.host.c_str(), data.dev.name.c_str(),
               data.intf.name.c_str(), data.intf.alias.c_str(), data.bcrate);

   fprintf(fp, "<IMG SRC=\"cid:graph.png\" ALT=\"Graph\">\r\n"
               "--bound\r\n"
               "Content-Location: CID:somelocation\n"
               "Content-ID: <graph.png>\n"
               "Content-Type: IMAGE/PNG\n"
               "Content-Transfer-Encoding: BASE64\n\n");

   BIO *b64 = BIO_new(BIO_f_base64());
   BIO *bio = BIO_new_fp(fp, BIO_NOCLOSE);
   BIO_push(b64, bio);

   int rval;
   const size_t bufsize = 512;
   char imgbuf[bufsize];

   while (0 < (rval = read(fd, imgbuf, bufsize))) BIO_write(b64, imgbuf, rval);
   (void) BIO_flush(b64);
   BIO_free_all(b64);
   close(fd);

   return fp;
}

void send_alarm(const device *dev, int_info *intf, unsigned long bcrate)
{
   static const char *funcname {"send_alarm"};
   static const conf::string_t &datadir {config["datadir"].get<conf::string_t>()};

   static const conf::integer_t xsize {config["notifier"]["image-width"].get<conf::integer_t>()};
   static const conf::integer_t ysize {config["notifier"]["image-height"].get<conf::integer_t>()};

   static const conf::string_t &from {config["notifier"]["from"].get<conf::string_t>()};   
   static const conf::multistring_t &rcpts {config["notifier"]["rcpts"].get<conf::multistring_t>()};
   static const conf::string_t smtphost {config["notifier"]["smtphost"].get<conf::string_t>()};

   buffer filename, title;
   filename.print("%s/graph.png", datadir.c_str());
   title.print("%s: %s - %s", dev->host.c_str(), intf->name.c_str(), intf->alias.c_str());
   intf->rrdata.graph(filename.data(), title.data(), xsize, ysize);

   msgdata maildata {from, rcpts, *dev, *intf, bcrate};
   FILE *data = generate_message(filename.data(), maildata);
   remove(filename.data());

   CURL *curl = curl_easy_init();
   if (nullptr == curl) throw logging::error {funcname, "curl_easy_init failed."};

   curl_easy_setopt(curl, CURLOPT_URL, smtphost.c_str());
   curl_easy_setopt(curl, CURLOPT_MAIL_FROM, from.c_str());

   curl_slist *recipients {};
   for (const auto &to : rcpts) recipients = curl_slist_append(recipients, to.c_str());
   curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);

   rewind(data);
   curl_easy_setopt(curl, CURLOPT_READDATA, data);

   CURLcode res = curl_easy_perform(curl);
   if (CURLE_OK != res) throw logging::error {funcname, "curl_easy_perform() failed: %s",
      curl_easy_strerror(res)};

   curl_slist_free_all(recipients);
   curl_easy_cleanup(curl);
   fclose(data);
}

void process_alarms(std::unique_lock<std::mutex> &datalock)
{
   static const char *funcname {"process_alarms"};

   alarm_info data;
   unsigned long bcrate {};

   for (;;)
   {
      if (0 == alarm_data.size()) return;
      data = alarm_data.back();
      alarm_data.pop_back();
      datalock.unlock();

      // NOTE: i don't know how i came up with this formula.
      // If we have a spike rechecking broadcast pps with 2 seconds measure to confirm, that we need to send alert.
      if (alarmtype::spike == data.intf->data.alarm) bcrate = check_bc_rate(data.dev, data.intf->id);
      if (0 != bcrate and bcrate < data.intf->data.lastmav * data.intf->data.mav_vals.size() * 0.8)
      {
         logger.log_message(LOG_INFO, funcname, "%s: alarm has not been sent. Two second rate: %lu. Calculated: %02.f",
               data.dev->host.c_str(), bcrate, data.intf->data.lastmav * data.intf->data.mav_vals.size() * 0.8);
      }

      else send_alarm(data.dev, data.intf, bcrate);
      datalock.lock();
   }
}

void workloop(thread_sync *syncdata)
{
   static const char *funcname {"workloop"};
   bool exit {false};

   std::vector<device *> polldevs;
   std::unique_lock<std::mutex> datalock {syncdata->worker_datalock};

   for (;;)
   {
      if (0 == action_data.size() and 0 == alarm_data.size())
      {
         if (0 == polldevs.size())
         {
            datalock.unlock();
            logger.log_message(LOG_INFO, funcname, "No jobs available - waiting on condition variable.");

            std::unique_lock<std::mutex> statelock {syncdata->statelock};
            syncdata->sleeping = true;

            while (syncdata->sleeping) syncdata->wake.wait(statelock);
            if (!syncdata->running) exit = true;
            datalock.lock();            
         }

         // NOTE: else sleep for some time and recheck host timers
      }

      if (0 != alarm_data.size()) process_alarms(datalock);
      if (exit) return;

      // NOTE: we should actually do something.
      if (0 != action_data.size()) action_data.clear();
   }   
}

void worker(thread_sync *syncdata)
{
   static const char *funcname {"worker"};

   try { workloop(syncdata); }

   catch (logging::error &error) {
      logger.error_exit(funcname, "Exception thrown in worker thread: %s", error.what());
   }

   catch (...) {
      logger.error_exit(funcname, "Worker thread aborted by generic catch clause.");
   }
}
