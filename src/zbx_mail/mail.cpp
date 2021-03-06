#include <sys/stat.h>
#include <sys/file.h>
#include <sys/types.h>

#include <errno.h>
#include <string.h>
#include <dirent.h>

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <curl/curl.h>

#include "prog_config.h"
#include "aux_log.h"
#include "mail.h"

namespace mail {

namespace {
   const std::string msg_ext = ".msg";
   const std::string tmp_ext = ".tmp";
   const std::string att_ext = ".att";
}

size_t callback_read(void *ptr, size_t size, size_t nmemb, void *userp)
{
   if ((0 == size) or (0 == nmemb) or (1 > size * nmemb)) return 0;
   callback_struct *cbs = static_cast<callback_struct *>(userp);

   size_t have = cbs->buf->size() - cbs->bytes_read;
   if (0 == have) return 0;

   size_t requested = nmemb * size;
   size_t send = (requested < have) ? requested : have;
   memcpy(ptr, cbs->buf->data() + cbs->bytes_read, send);
   cbs->bytes_read += send;
   return send;
}

void mail_message::add_error(const char *format, ...)
{
   va_list args;   
   error_id++;

   strbuf.print("(%lu) ", error_id);
   eid_str = strbuf.data();

   va_start(args, format);
   strbuf.vappend(format, args);
   va_end(args);
   errors << strbuf.data() << std::endl;
}

void mail_message::add_post(const char *format, ...)
{
   va_list args;   
   va_start(args, format);
   strbuf.print(format, args);
   va_end(args);
   post << strbuf.data() << std::endl;
}

void mail_message::add_image(const char *name, const char *data, size_t size)
{
   emb_image eimage;
   eimage.name = name;
   eimage.data.append(data, size);
   imgs.push_back(std::move(eimage));
}

void mail_message::make_digest(const char *data)
{
   unsigned char digest[MD5_DIGEST_LENGTH];

   MD5(reinterpret_cast<const unsigned char *>(data), strlen(data), digest);
   for (int i = 0; i < MD5_DIGEST_LENGTH; i++)
      sprintf(digest_string + i*2, "%02x", digest[i]);   
}

void cache_cleanup()
{
   static const char *funcname = "cache_cleanup";

   DIR *dirp;
   struct dirent *entry;
   struct stat filestat;

   if (nullptr == (dirp = opendir(config["cache-dir"].get<conf::string_t>().c_str())))
      throw logging::error(funcname, "Cannot open cache directory '%s' : %s",
            config["cache-dir"].get<conf::string_t>().c_str(), strerror(errno));

   std::string filename;
   while (nullptr != (entry = readdir(dirp)))
   {
      if (nullptr == strstr(entry->d_name, msg_ext.c_str())) continue;
      filename = config["cache-dir"].get<conf::string_t>() + '/';
      filename += entry->d_name;

      if (-1 == stat(filename.c_str(), &filestat))
         throw logging::error(funcname, "Error after stat '%s' : %s", filename.c_str(), strerror(errno));
      if ((sys_clock::to_time_t(sys_clock::now()) - filestat.st_atime) > config["cache-timeout"].get<conf::integer_t>()) remove(filename.c_str());
   }
}

cache_status mail_message::get_cache_stat(std::string &filename)
{
   static const char *funcname = "mail_message::get_cache_stat";
   struct stat cache_stat;

   if (-1 == stat(filename.c_str(), &cache_stat))
   {
      if (ENOENT == errno)
      {
         if (-1 == stat(config["cache-dir"].get<conf::string_t>().c_str(), &cache_stat))
         {
            logger.log_message(LOG_ERR, funcname, "Configuired cache dir '%s' is probably wrong. "
                               "Can't access it: %s", config["cache-dir"].get<conf::string_t>().c_str(), strerror(errno));
            return cache_status::cache_unaccessible;
         }
         return cache_status::cache_msg_notfound;
      }

      throw logging::error(funcname, "Can't get filestat for cache-file '%s' : %s",
                           filename.c_str(), strerror(errno));
   }

   if ((sys_clock::to_time_t(sys_clock::now()) - cache_stat.st_atime) > config["cache-timeout"].get<conf::integer_t>())
   {
      remove(filename.c_str());
      return cache_status::cache_msg_notfound;
   }

   return cache_status::cache_msg_found;
}

void mail_message::open_cache(const std::string &filename, const char *mode)
{
   static const char *funcname = "mail_message::open_cache";
   std::string name = filename;

   if (nullptr == (msg_fp = fopen(name.c_str(), mode)))
      throw logging::error(funcname, "Cannot open body cache-file '%s' : %s",
            name.c_str(), strerror(errno));

   name += att_ext;
   if (nullptr == (attach_fp = fopen(name.c_str(), mode)))
   {
      if (ENOENT == errno) return;
      throw logging::error(funcname, "Cannot open attach cache-file '%s' : %s",
            name.c_str(), strerror(errno));
   }
}

cache_status mail_message::try_cache(const char *body)
{
   static const char *funcname = "mail_message::try_cache";

   make_digest(body);
   cache_filename = config["cache-dir"].get<conf::string_t>() + '/';
   cache_filename.append(digest_string, MD5_DIGEST_LENGTH * 2);
   cache_filename += ".msg";

   cache_state = get_cache_stat(cache_filename);

   if (cache_status::cache_unaccessible == cache_state) return cache_state;
   if (cache_status::cache_msg_found == cache_state)
   {
      open_cache(cache_filename, "r");
      return cache_state;
   }

   std::string tmp_filename = cache_filename + tmp_ext;
   int fd = open(tmp_filename.c_str(), O_RDWR | O_CREAT, 0600);
   if (-1 == fd) throw logging::error(funcname, "Cannot open temp msg-cache '%s' : %s", tmp_filename.c_str(), strerror(errno));

   // Cache msg was not found and we've successfuly set lock on the file.
   // From now on generate_message can do it work and rename file for further access.
   if (0 == flock(fd, LOCK_EX | LOCK_NB)) 
   {
      msg_fp = fdopen(fd, "w+");
      tmp_filename.replace(tmp_filename.size() - tmp_ext.size(), att_ext.size(), att_ext);
      if (nullptr == (attach_fp = fopen(tmp_filename.c_str(), "w+")))
        throw logging::error(funcname,  "Attachment file '%s' cannot be opened: %s", tmp_filename.c_str(), strerror(errno));
      return cache_state;
   }

   // If lock fails, than other processing is working on message now. 
   // So we're just waiting for him to end his work. Then send his message from cache.
   if (EWOULDBLOCK != errno)
      throw logging::error("Cannot set flock due to unexpected error '%s' : %s", tmp_filename.c_str(), strerror(errno));

   for (int i = 0; cache_status::cache_msg_notfound == get_cache_stat(cache_filename); i++)
   {
      sleep(i);
      if (i > 10) throw logging::error(funcname, "Still no result after 10 tries of waiting on cache file '%s'", cache_filename.c_str());
   }

   open_cache(cache_filename, "r");
   return cache_state;
}

void mail_message::generate_message()
{
   static const char *funcname = "mail_message::generate_message";

   if (cache_status::cache_unaccessible == cache_state)
   {
      if (nullptr == (msg_fp = tmpfile()))
         throw logging::error(funcname, "Failed to create temporary file: %s", strerror(errno));
      if (nullptr == (attach_fp = tmpfile()))
         throw logging::error(funcname, "Failed to create temporary file: %s", strerror(errno));      
   }

   std::string line;
   std::string fullfrom = config["smtp-from-name"].get<conf::string_t>() + config["smtp-from"].get<conf::string_t>();

   fprintf(msg_fp, "From: %s\r\n"
                   "Subject: %s\r\n"
                   "Mime-Version: 1.0\r\n"
                   "Content-Type: multipart/mixed; boundary=\"%s\"\r\n"
                   "\r\n"
                   "--%s\r\n"
                   "Content-Type: text/html; charset=\"UTF-8\"\r\n\r\n"
                   "%s\n",
                   fullfrom.c_str(), subject.c_str(), 
                   digest_string, digest_string, body.c_str());

   errors.peek();
   if (!errors.eof())
   {
      fprintf(msg_fp, "<br><b> --- Error section: </b><br>");
      for (; getline(errors, line); ) fprintf(msg_fp, "%s<br>", line.c_str());
   }

   post.peek();
   if (!post.eof()) for (; getline(post, line); ) fprintf(msg_fp, "%s<br>", line.c_str());

   if (0 != imgs.size()) 
   {
      BIO *b64 = BIO_new(BIO_f_base64());
      BIO *bio = BIO_new_fp(attach_fp, BIO_NOCLOSE);
      BIO_push(b64, bio);

      for (auto &image : imgs)
      {
         fprintf(attach_fp, "\r\n--%s\r\n"
                            "Content-Location: CID:somelocation\n"
                            "Content-ID: <%s>\n"
                            "Content-Type: IMAGE/PNG\n"
                            "Content-Transfer-Encoding: BASE64\n\n", 
                            digest_string, image.name.c_str());
         BIO_write(b64, image.data.c_str(), image.data.size());
         (void) BIO_flush(b64);  // cast away warning. nasty macros.
      }
      BIO_free_all(b64);
   }


   std::chrono::duration<double> elapsed = sted_clock::now() - time_start;
   fprintf(msg_fp, "<br><br> --- <br>ZBXM Cache ID: %s<br>Generated in: %f<br>", digest_string, elapsed.count());

   fflush(msg_fp);
   fflush(attach_fp);

   if (cache_status::cache_unaccessible == cache_state) return;
   std::string tmpname = cache_filename + ".tmp";
   if (-1 == rename(tmpname.c_str(), cache_filename.c_str()))
      throw logging::error(funcname, "Rename '%s'-'%s' failed: %s",
            tmpname.c_str(), cache_filename.c_str(), strerror(errno));   
}

void mail_message::fill_message(buffer &msgbuf)
{
   static const char *funcname = "mail_message::fill_message";

   msgbuf.print("To: %s\r\n", to.c_str());
   rewind(msg_fp);
   rewind(attach_fp);

   int bytes = 0;
   size_t bufsize = 0;
   char *filebuf = nullptr;

   while (0 < (bytes = getline(&filebuf, &bufsize, msg_fp))) msgbuf.mappend(filebuf, bytes);
   if (!feof(msg_fp)) throw logging::error(funcname, "Error while read: %s", strerror(errno));
   if (cache_status::cache_msg_found == cache_state) msgbuf.append("[CR] - ");

   std::chrono::duration<double> elapsed = sted_clock::now() - time_start;
   msgbuf.append("Approximate processing time: %f\r\n", elapsed.count());

   while (0 < (bytes = getline(&filebuf, &bufsize, attach_fp))) msgbuf.mappend(filebuf, bytes);
   if (!feof(msg_fp)) throw logging::error(funcname, "Error while read: %s", strerror(errno));
   free(filebuf);
}

void mail_message::send(const char *body)
{
   static const char *funcname = "mail_message::send";

   CURL *curl;
   CURLcode res;

   if (nullptr == (curl = curl_easy_init()))
     throw logging::error(funcname, "curl_easy_init failed");

   curl_easy_setopt(curl, CURLOPT_URL, config["smtp-host"].get<conf::string_t>().c_str());
   curl_easy_setopt(curl, CURLOPT_MAIL_FROM, from.c_str());
   struct curl_slist *recipients = curl_slist_append(nullptr, to.c_str());
   curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);

   buffer message;
   callback_struct cbs(0, &message);

   if (nullptr == body) fill_message(message);
   else
   {
      make_digest(body);
      std::string fullfrom = config["smtp-from-name"].get<conf::string_t>() + config["smtp-from"].get<conf::string_t>();
      message.print("From: %s\r\n"
                    "Subject: %s\r\n"
                    "Mime-Version: 1.0\r\n"
                    "Content-Type: multipart/mixed; boundary=\"%s\"\r\n"
                    "\r\n"
                    "--%s\r\n"
                    "Content-Type: text/html; charset=\"UTF-8\"\r\n\r\n"
                    "%s\n",
                    fullfrom.c_str(), subject.c_str(), 
                    digest_string, digest_string, body);
   }

   curl_easy_setopt(curl, CURLOPT_READFUNCTION, callback_read);
   curl_easy_setopt(curl, CURLOPT_READDATA, &cbs);
   curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L); 

   if (CURLE_OK != (res = curl_easy_perform(curl)))
      throw logging::error(funcname, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
   curl_slist_free_all(recipients);
   curl_easy_cleanup(curl);   
}

} // MAIL NAMESPACE

