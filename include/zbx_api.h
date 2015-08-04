#ifndef ZBX_API_H
#define ZBX_API_H

#include "curl_cl.h"
#include "frozen.h"
#include "typedef.h"

namespace zbx_api {

class api_session
{
   public:
      // Received data from server will be parsed and stored here, so it must be accessible.
      json_token *arr;
      json_token *tok;

      api_session() : arr(nullptr), active(false), req_id(1) { }
      ~api_session() noexcept;

      void set_auth(const std::string &i_url, const std::string &i_user, const std::string &i_password);
      bool json_get_uint(const char *json_path, uint_t *result);

      int send_vstr(const char *format, ...);
      int send_plain(const char *send_buffer);
      
   private:
      bool active;
      uint_t req_id;

      std::unique_ptr<char> auth_token;

      std::string url;
      std::string username;
      std::string password;

      basic_curl::basic_http api_conn;

      void init();
      int send_json(const char *send_buffer);
};   

} // ZBX_API NAMESPACE

#endif
