#include <stdexcept>
#include <cstring>
#include <sstream>

#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>

#include "aux_log.h"
#include "frozen.h"

#include "zbx_sender.h"

ssize_t tcp_stream::send(const void *buffer, size_t len, int flags)
{
   size_t total = 0;
   const char *ptr = static_cast<const char *>(buffer);

   if (-1 == sd) throw std::runtime_error {"no active connection."};
   for (ssize_t n = 0; total < len; total += n)
   {
      n = ::send(sd, ptr + total, len - total, flags);
      if (-1 == n) return -1;
   }
   return total;
}

ssize_t tcp_stream::recv(void *buffer, size_t len, int flags)
{
   if (-1 == sd) throw std::runtime_error {"no active connection"};
   return ::recv(sd, buffer, len, flags);
}

ssize_t tcp_client::send(const void *buffer, size_t len, int flags)
{
   ssize_t result = tcp_stream::send(buffer, len, flags);
   if (-1 == result) throw std::runtime_error {std::string{strerror(errno)} + " in send to: " + peer};
   return result;
}

ssize_t tcp_client::recv(void *buffer, size_t len, int flags)
{
   ssize_t result = tcp_stream::recv(buffer, len, flags);
   if (-1 == result) throw std::runtime_error {std::string{strerror(errno)} + " in recv from: " + peer};
   return result;
}

tcp_client::tcp_client(const char *peer_, unsigned port_) : port{port_}, peer{peer_}
{
   struct sockaddr_in address;

   memset(&address, 0, sizeof(address));
   address.sin_family = AF_INET;
   address.sin_port = htons(port);

   if (0 != resolve_hostname(peer.c_str(), &(address.sin_addr)))
      inet_pton(PF_INET, peer.c_str(), &(address.sin_addr));

   sd = socket(AF_INET, SOCK_STREAM, 0);
   if (-1 == connect(sd, (struct sockaddr *) &address, sizeof(address)))
      throw std::runtime_error {peer + ": " + std::string{strerror(errno)}};
}

int tcp_client::resolve_hostname(const char *hostname, struct in_addr *addr)
{
   struct addrinfo *res;
   int result = getaddrinfo(hostname, nullptr, nullptr, &res);

   if (0 == result)
   {
      memcpy(addr, &((struct sockaddr_in *) res->ai_addr)->sin_addr, sizeof(struct in_addr));
      freeaddrinfo(res);
   }
   return result;
}

const char zbx_sender::header[] {'Z', 'B', 'X', 'D', 1};

void zbx_sender::build_data()
{
   bool data_clock {false};
   databuf.clear();
   databuf.print(R"**({ "request" : "sender data", "data": [)**");

   for (const auto &entry : data)
   {
      if (0 != entry.clock)
      {
         databuf.append(R"**({"host":"%s","key":"%s","value":"%s", "clock": %ld},)**", 
               entry.host.c_str(), entry.key.c_str(), entry.value.c_str(), entry.clock);
         data_clock = true;
      }

      else databuf.append(R"**({"host":"%s","key":"%s","value":"%s"},)**", 
            entry.host.c_str(), entry.key.c_str(), entry.value.c_str());
   }

   databuf.pop_back();
   if (data_clock) databuf.append(R"**(], "clock" : %ld})**", time(nullptr));
   else databuf.append("]}");
}

void zbx_sender::send(bool build)
{
   static const char *funcname {"zbx_sender::send"};
   static const char *successfull {"success"};
   static int suclen = strlen(successfull);

   if (build) 
   {
      if (0 == data.size()) return;
      build_data();
   }

   datalen = databuf.size();
   tcp_client::send(header, sizeof(header));
   tcp_client::send(&datalen, sizeof(datalen));
   tcp_client::send(databuf.data(), datalen);

   size_t offset = data_offset;
   // Buffer should be at least 1Kb already, which should be enough to store response.
   size_t len = tcp_client::recv(databuf.mem(), databuf.capacity()); 

   if (sizeof(header) == len)
   {
      if (0 != memcmp(header, databuf.data(), sizeof(header)))
         throw logging::error(funcname, "unexpected header in response");
      offset -= sizeof(header);
      len = tcp_client::recv(databuf.mem(), databuf.capacity());
   }

   else
   {
      if (len < data_offset) throw logging::error(funcname, "unexpected response length: %lu", len);
      if (0 != memcmp(header, databuf.data(), sizeof(header)))
         throw logging::error(funcname, "unexpected header in response");
   }

   json_token *tok;   
   parse_json(databuf.data() + offset, len - offset, tokarr.get(), json_arrsize);
   if (nullptr == (tok = find_json_token(tokarr.get(), "response")) or
       0 != strncmp(tok->ptr, successfull, tok->len < suclen ? tok->len : suclen))
      throw logging::error(funcname, "unexpected response string: %.*s", tok->len, tok->ptr);
}
