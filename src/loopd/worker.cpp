#include <cstdio>
#include <fcntl.h>
#include <curl/curl.h>

#include <openssl/bio.h>
#include <openssl/evp.h>

#include <list>

#include "snmp/oids.h"

#include "aux_log.h"
#include "worker.h"
#include "data.h"

unsigned long check_bc_rate(const device *dev, unsigned intnum)
{
   static const char *funcname {"check_bc_rate"};
   static const std::chrono::seconds interval {config["poller"]["recheck-interval"].get<conf::integer_t>()};

   netsnmp_pdu *req;
   snmp::pdu_handle response;
   uint64_t counter {}, delta {};

   snmp::sess_handle sessp {snmp::init_snmp_session(dev->host.c_str(), dev->community.c_str())};   
   for (unsigned i = 0; i < 2; i++)
   {
      req = snmp_pdu_create(SNMP_MSG_GET);
      snmp_add_null_var(req, snmp::oids::ifbroadcast, snmp::oids::ifbroadcast_size);
      req->variables->name[snmp::oids::ifbroadcast_size - 1] = intnum;

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

FILE * generate_message(alarm_info &data, unsigned long bcrate)
{
   static const char *funcname {"generate_message"};
   static const conf::string_t graphfile {config["datadir"].get<conf::string_t>() + "/graph.png"};

   static const conf::integer_t xsize {config["notifier"]["image-width"].get<conf::integer_t>()};
   static const conf::integer_t ysize {config["notifier"]["image-height"].get<conf::integer_t>()};
   static const conf::string_t &from {config["notifier"]["from"].get<conf::string_t>()};
   static const conf::multistring_t &rcpts {config["notifier"]["rcpts"].get<conf::multistring_t>()};   

   int_info &intf = *(data.intf);
   buffer title;

   title.print("%s: %s - %s", data.dev->host.c_str(), intf.name.c_str(), intf.alias.c_str());
   intf.rrdata.graph(graphfile.c_str(), title.data(), xsize, ysize);

   int fd = open(graphfile.c_str(), O_RDONLY);
   if (-1 == fd) throw logging::error {funcname, "Failed to open graph file '%s': %s.",
      graphfile.c_str(), strerror(errno)};

   FILE *fp = tmpfile();
   if (nullptr == fp) throw logging::error {funcname, "Failed to create temporary datafile."};

   fprintf(fp, "From: %s\r\n", from.c_str());
   for (const auto &to : rcpts) fprintf(fp, "To: %s\r\n", to.c_str());

   fprintf(fp, "Subject: %s: High broadcast pps level - %s\r\n"
               "Mime-Version: 1.0\r\n"
               "Content-Type: multipart/related; boundary=\"bound\"\r\n"
               "\r\n"
               "--bound\r\n"
               "Content-Type: text/html; charset=\"UTF-8\"\r\n\r\n",
           data.dev->host.c_str(), intf.name.c_str());

   fprintf(fp, "High broadcast pps level detected on device: %s - %s<br>\n"
               "Interface: %s - %s<br>\n"
               "Alarm type: <b>%s</b><br>\n",
           data.dev->host.c_str(), data.dev->name.c_str(), intf.name.c_str(),
           intf.alias.c_str(), alarmtype_names[intf.data.alarm].c_str());

   if (alarmtype::spike == intf.data.alarm)
      fprintf(fp, "Broadcast pps measured in last 2 seconds: %lu<br>\n", bcrate);
   fprintf(fp, "<br>\n");

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

void send_message(FILE *data)
{
   static const char *funcname {"send_message"};
   static const conf::string_t &from {config["notifier"]["from"].get<conf::string_t>()};
   static const conf::multistring_t &rcpts {config["notifier"]["rcpts"].get<conf::multistring_t>()};
   static const conf::string_t &smtphost {config["notifier"]["smtphost"].get<conf::string_t>()};

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
}

void process_alarms(std::unique_lock<std::mutex> &datalock)
{
   static const char *funcname {"process_alarms"};
   static const double bcmax_c  {config["poller"]["bcmax"].get<conf::integer_t>() * 0.8};
   static const double mavmax_c {config["poller"]["mavmax"].get<conf::integer_t>() * 0.8};

   unsigned long bcrate {};
   double calc {};

   for (alarm_info data; !alarm_data.empty();)
   {
      data = alarm_data.back();
      alarm_data.pop_back();
      datalock.unlock();

      bcrate = check_bc_rate(data.dev, data.intf->id);
      switch (data.intf->data.alarm)
      {
         case alarmtype::spike:  calc = data.intf->data.lastmav * data.intf->data.mav_vals.size() * 0.5; break;
         case alarmtype::bcmax:  calc = bcmax_c; break;
         case alarmtype::mavmax: calc = mavmax_c; break;
         default: throw logging::error {funcname, "%s: unexpected alarm type", data.dev->host.c_str()};
      }

      if (0 != bcrate and bcrate < calc)
      {
            logger.log_message(LOG_INFO, funcname, "%s: alarm has not been sent. Rechecked broadcast rate: %lu. "
                  "Calculated: %02.f", data.dev->host.c_str(), bcrate, calc);
            datalock.lock();
            continue;
      }

      FILE *message = generate_message(data, bcrate);
      send_message(message);
      fclose(message);
      datalock.lock();
   }
}

void return_dev(device &dev)
{
   std::vector<unsigned> intdel;

   for (auto &it : dev.ints)
   {
      if (false == it.second.delmark) continue;
      it.second.rrdata.remove();
      intdel.push_back(it.first);
   }

   for (auto n : intdel) dev.ints.erase(n);
   dev.reset();
   
   prepare_request(dev);
   return_data.push_back(&dev);
}

void process_devices(std::unique_lock<std::mutex> &datalock, std::list<device *> &polldevs, thread_sync *syncdata)
{
   static const char *funcname {"process_devices"};
   static const unsigned retry_interval {10};
   static const unsigned max_backoff {1024};

   if (!action_data.empty())
   {
      for (auto &it : action_data)
      {
         it->timeticks = 0;
         polldevs.push_back(it);
         logger.log_message(LOG_INFO, funcname, "%s: new device task added.", it->host.c_str());
      }

      action_data.clear();
   }

   datalock.unlock();
   time_t rawtime {time(nullptr)};

   for (auto it : polldevs)
   {
      device &dev = *it;
      if (rawtime < dev.timeticks) continue;

      init_device(dev);
      if (hoststate::enabled == dev.state) update_ints(dev);

      // Checking again in case we failed while interfaces update.
      if (hoststate::enabled != dev.state)
      {
         dev.timeticks = ((0 == dev.timeticks) ? time(nullptr) : dev.timeticks) + retry_interval * dev.wait_backoff;
         if (dev.wait_backoff < max_backoff) dev.wait_backoff *= 2;

         logger.log_message(LOG_INFO, funcname, "%s: device is still unreachable. Increasing backoff to %u",
               dev.host.c_str(), dev.wait_backoff);
         continue;
      }

      logger.log_message(LOG_INFO, funcname, "%s: device is active. Passing back to the main thread.", dev.host.c_str());
      syncdata->updatelock.lock();

      return_dev(dev);
      syncdata->data_updated = true;
      syncdata->updatelock.unlock();
   }

   polldevs.remove_if([](device *dev) { return dev->state == hoststate::enabled; });
   datalock.lock();
}

void workloop(thread_sync *syncdata)
{
   static const char *funcname {"workloop"};
   static const std::chrono::seconds interval {2};

   std::list<device *> polldevs;
   std::unique_lock<std::mutex> datalock {syncdata->worker_datalock};
   std::unique_lock<std::mutex> statelock {syncdata->statelock, std::defer_lock};

   for (;;)
   {
      if (action_data.empty() and alarm_data.empty())
      {
         datalock.unlock();

         if (polldevs.empty())
         {
            statelock.lock();
            logger.log_message(LOG_INFO, funcname, "No jobs available - waiting on condition variable.");

            syncdata->sleeping = true;
            while (syncdata->sleeping) syncdata->wake.wait(statelock);
            statelock.unlock();
         }

         else std::this_thread::sleep_for(interval);
         datalock.lock();
      }

      if (!alarm_data.empty()) process_alarms(datalock);

      statelock.lock();
      if (!syncdata->running) return;
      statelock.unlock();

      process_devices(datalock, polldevs, syncdata);
   }
}

void worker(thread_sync *syncdata)
{
   static const char *funcname {"worker"};

   try { workloop(syncdata); }

   catch (std::exception &exc) {
      logger.error_exit(funcname, "Exception thrown in worker thread: %s", exc.what());
   }

   catch (...) {
      logger.error_exit(funcname, "Worker thread aborted by generic catch clause.");
   }
}
