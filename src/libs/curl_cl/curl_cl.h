#ifndef BASIC_CURL_CONTAINER
#define BASIC_CURL_CONTAINER

#include <string>
#include <curl/curl.h>

#include "buffer.h"

namespace basic_curl {

class basic_http
{
   public:
      buffer send_data;
      buffer recv_data;

      basic_http();
      ~basic_http() noexcept { curl_easy_cleanup(curl); }

      void add_header(const char *str)
      {
         headers = curl_slist_append(headers, str);
         curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
      }

      void set_default_url(const char *str) { default_url = str; }
      void set_default_url(const std::string &str) { default_url = str; }

      CURLcode get(const char *url);      

      CURLcode post(const std::string &url) { return i_post(url.c_str()); }
      CURLcode post(const char *url) { return i_post(url); }
      CURLcode post() { return i_post(default_url.c_str()); }

      CURLcode cookie_login(const char *cookie_file) { return i_cookie_login(cookie_file, default_url.c_str()); }
      CURLcode cookie_login(const std::string &cookie_file) { return i_cookie_login(cookie_file.c_str(), default_url.c_str()); }
      CURLcode cookie_login(const char *cookie_file, const char *url) { return i_cookie_login(cookie_file, url); }
      CURLcode cookie_login(const std::string &cookie_file, const std::string &url) { 
         return i_cookie_login(cookie_file.c_str(), url.c_str()); }

   private:
      CURL *curl;
      CURLcode res;
      struct curl_slist *headers;

      std::string default_url;

      CURLcode i_post(const char *url);
      CURLcode i_cookie_login(const char *cookie_file, const char *url);
};

} // BASIC_CURL NAMESPACE


#endif
