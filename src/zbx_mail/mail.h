#ifndef ZBX_MAILER_MAIL_H
#define ZBX_MAILER_MAIL_H

#include <vector>
#include <string>
#include <sstream>

#include <openssl/md5.h>

#include "time.h"
#include "typedef.h"
#include "buffer.h"

namespace mail {

struct emb_image
{
   std::string name;
   std::string data;
};

struct callback_struct
{
   size_t bytes_read;
   buffer *buf;

   callback_struct(int bytes, buffer *buf_) : bytes_read(bytes), buf(buf_) { }
};

enum class cache_status {
   CACHE_UNKNOWN = 0,
   CACHE_MSG_FOUND,
   CACHE_MSG_NOTFOUND,
   CACHE_UNACCESSIBLE
};

class mail_message
{
   public:
      mail_message() : cache_state(cache_status::CACHE_UNKNOWN), msg_fp(nullptr), error_id(0) { }

      inline void set_start() { clock_gettime(CLOCK_MONOTONIC, &start); }
      inline void add_line(const std::string &str) { body += str; }

      inline void set_to(const char *str) { to = str; }
      inline void set_to(const std::string &str) { to = str; }

      inline void set_subject(const char *str) { subject = str;  }
      inline void set_subject(const std::string &str) { subject = str;  }      

      inline void set_from(const char *str) { from = str; }
      inline void set_from(const std::string &str) { from = str; }

      inline const std::string & get_eid() const { return eid_str; }

      void add_error(const char *format, ...) __attribute__((format(printf,2,3)));
      void add_post(const char *format, ...) __attribute__((format(printf,2,3)));      
      void add_image(const char *name, const char *data, size_t size);

      cache_status try_cache(const char *body);
      void generate_message();
      void send();

   private:
      struct timespec start;
      char digest_string[(MD5_DIGEST_LENGTH * 2) + 1];

      cache_status cache_state;
      std::string cache_filename;
      FILE *msg_fp;
      FILE *attach_fp;

      uint_t error_id;
      buffer strbuf;

      std::string to;
      std::string subject;
      std::string from;
      std::string body;

      std::string eid_str;
      std::stringstream errors;
      std::stringstream post;

      std::vector<emb_image> imgs;

      void open_cache(const std::string &filename, const char *mode);
      cache_status get_cache_stat(std::string &filename);
      void fill_message(buffer &msgbuf);
};

void cache_cleanup();

} // MAIL NAMESPACE

#endif
