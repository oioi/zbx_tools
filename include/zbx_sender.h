#ifndef ZBX_L_SENDER_H
#define ZBX_L_SENDER_H

#include <string>
#include <vector>
#include <sstream>
#include <unistd.h>

#include "frozen.h"
#include "buffer.h"

class tcp_stream
{
   protected:
      ~tcp_stream() { if (-1 != sd) close(sd); }

      ssize_t send(const void *buffer, size_t len, int flags);
      ssize_t recv(void *buffer, size_t len, int flags);

      int sd {-1};
};

class tcp_client : private tcp_stream
{
   public:
      tcp_client(const char *peer_, unsigned port_);

      ssize_t send(const void *buffer, size_t len, int flags = 0);
      ssize_t recv(void *buffer, size_t len, int flags = 0);

      void set_recv_timeout(const timeval &tv) {
         setsockopt(sd, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv, sizeof(timeval)); }

   private:
      unsigned port;
      std::string peer;

      int resolve_hostname(const char *hostname, struct in_addr *addr);
};

struct sender_data
{
   sender_data(const std::string &host_, const std::string &key_, const std::string &value_, time_t clock_) :
      host{host_}, key{key_}, value{value_}, clock{clock_} { }

   std::string host;
   std::string key;
   std::string value;
   time_t clock;
};

struct sender_response
{
   unsigned processed {};
   unsigned failed    {};
   unsigned total     {};
   double   elapsed   {};
};

class zbx_sender : private tcp_client
{
   public:
      zbx_sender(const char *peer = "127.0.0.1", unsigned port = 10051) :
         tcp_client{peer, port}, datalen{}, tokarr{new json_token[json_arrsize]} { }

      void clear() { data.clear(); }      
      sender_response send(bool build = true);
      // In case someone wants to build data on their own.
      sender_response send(const char *data, size_t len) { databuf.clear(); databuf.mappend(data, len); return send(false); }

      template <typename T>
      void add_data(const std::string &host, const std::string &key, const T &val, time_t clock = 0);

   private:
      static const char header[];
      static const size_t data_offset {13};
      static const size_t json_arrsize {10};

      buffer databuf;
      uint64_t datalen;
      std::vector<sender_data> data;
      std::unique_ptr<json_token []> tokarr;

      void build_data();
};

template <typename T>
void zbx_sender::add_data(const std::string &host, const std::string &key, const T &val, time_t clock)
{
   std::ostringstream ss;
   ss << val;
   data.emplace_back(host, key, ss.str(), clock);
}

#endif
