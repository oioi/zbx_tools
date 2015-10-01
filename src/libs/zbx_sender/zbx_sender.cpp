#include <stdexcept>
#include <cstring>

#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>

#include "aux_log.h"
#include "frozen.h"

#include "zbx_sender.h"

ssize_t tcp_stream::send(const void *buffer, size_t len, int flags) const
{
   size_t total = 0;
   const char *ptr = static_cast<const char *>(buffer);

   if (-1 == sd) throw std::runtime_error {"no active connection"};
   for (ssize_t n = 0; total < len; total += n)
   {
      n = ::send(sd, ptr + total, len - total, flags);
      if (-1 == n) return -1;
   }
   return total;
}

ssize_t tcp_stream::recv(void *buffer, size_t len, int flags) const 
{
   if (-1 == sd) throw std::runtime_error {"no active connection"};
   return ::recv(sd, buffer, len, flags);
}

ssize_t tcp_client::send(const void *buffer, size_t len, int flags) const
{
   ssize_t result = tcp_stream::send(buffer, len, flags);
   if (-1 == result) throw std::runtime_error {std::string{strerror(errno)} + " in send to: " + peer};
   return result;
}

ssize_t tcp_client::recv(void *buffer, size_t len, int flags) const
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

void zbx_sender::send(const void *data, size_t len)
{
   static const char *funcname {"zbx_sender::send"};
   static const char *successfull {"success"};

   datalen = len;
   tcp_client::send(header, sizeof(header));
   tcp_client::send(&datalen, sizeof(datalen));
   tcp_client::send(data, len);

   // NOTE: this is actually a very basic and naive implementation of response reading.
   // All further code is based on assumption that we will receive all data in one piece,
   // because response should be small: around 90 bytes data + 13 bytes header.
   size_t bufsize = 1024;
   std::unique_ptr<char> buffer {new char[bufsize]};
   len = tcp_client::recv(buffer.get(), bufsize);

   if (len < data_offset) throw logging::error(funcname, "unexpected response length: %lu", len);
   if (0 != memcmp(header, buffer.get(), sizeof(header)))
      throw logging::error(funcname, "unexpected header in response");

   json_token *tok;
   int arrsize = 20;
   int suclen = strlen(successfull);

   std::unique_ptr<json_token> tokarr {new json_token[arrsize]};
   parse_json(buffer.get() + data_offset, len - data_offset, tokarr.get(), arrsize);

   if (nullptr == (tok = find_json_token(tokarr.get(), "response")) or
       0 != strncmp(tok->ptr, successfull, tok->len < suclen ? tok->len : suclen))
      throw logging::error(funcname, "unexpected response string: %.*s", tok->len, tok->ptr);
}
