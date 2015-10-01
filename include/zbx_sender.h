#ifndef ZBX_L_SENDER_H
#define ZBX_L_SENDER_H

#include <string>
#include <unistd.h>

class tcp_stream
{
   protected:
      ~tcp_stream() { if (-1 != sd) close(sd); }

      ssize_t send(const void *buffer, size_t len, int flags) const;
      ssize_t recv(void *buffer, size_t len, int flags) const;

      int sd {-1};
};

class tcp_client : private tcp_stream
{
   public:
      tcp_client(const char *peer_, unsigned port_);
      
      ssize_t send(const void *buffer, size_t len, int flags = 0) const;
      ssize_t recv(void *buffer, size_t len, int flags = 0) const;

   private:
      unsigned port;
      std::string peer;

      int resolve_hostname(const char *hostname, struct in_addr *addr);
};

class zbx_sender : private tcp_client
{
   public:
      zbx_sender(const char *peer, unsigned port) : tcp_client{peer, port}, datalen{} { }
      void send(const void *data, size_t len);

   private:
      uint64_t datalen;
      static const char header[];
      static const size_t data_offset {13};
};

#endif
