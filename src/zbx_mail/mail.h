#ifndef ZBX_MAILER_MAIL_H
#define ZBX_MAILER_MAIL_H

#include <vector>
#include <string>
#include <sstream>
#include <chrono>

#include <openssl/md5.h>

#include "typedef.h"
#include "buffer.h"

namespace mail {

using sys_clock = std::chrono::system_clock;
using sted_clock = std::chrono::steady_clock;

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
   cache_unknown = 0,
   cache_msg_found,
   cache_msg_notfound,
   cache_unaccessible
};

class mail_message
{
   public:
      mail_message() : cache_state(cache_status::cache_unknown), msg_fp(nullptr), error_id(0) { }

      void set_start() { time_start = sted_clock::now(); }
      void add_line(const std::string &str) { body += str; }

      void set_to(const char *str) { to = str; }
      void set_to(const std::string &str) { to = str; }

      void set_subject(const char *str) { subject = str;  }
      void set_subject(const std::string &str) { subject = str;  }      

      void set_from(const char *str) { from = str; }
      void set_from(const std::string &str) { from = str; }

      const std::string & get_eid() const { return eid_str; }
      cache_status cache() const { return cache_state; }

      void add_error(const char *format, ...) __attribute__((format(printf,2,3)));
      void add_post(const char *format, ...) __attribute__((format(printf,2,3)));      
      void add_image(const char *name, const char *data, size_t size);

      void make_digest(const char *data);
      cache_status try_cache(const char *body);
      void generate_message();
      void send(const char *body = nullptr);

   private:
      sted_clock::time_point time_start;
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
