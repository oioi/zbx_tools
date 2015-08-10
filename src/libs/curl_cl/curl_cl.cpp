#include "aux_log.h"
#include "curl_cl.h"

namespace basic_curl {

size_t curl_write_callback(void *contents, size_t size, size_t nmemb, void *userp)
{
   size_t realsize = size * nmemb;
   static_cast<buffer *>(userp)->mappend(static_cast<char *>(contents), realsize);
   return realsize;
}

basic_http::basic_http() : headers(nullptr)
{
   if (nullptr == (curl = curl_easy_init())) 
      throw logging::error("curl_conatiner()", "curl_easy_init fail inside container constructor.");

   curl_easy_setopt(curl, CURLOPT_USERAGENT, "libcurl-agent/1.0");   
   curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &curl_write_callback);
   curl_easy_setopt(curl, CURLOPT_WRITEDATA, static_cast<void *>(&recv_data));
}

CURLcode basic_http::i_post(const char *url)
{
   curl_easy_setopt(curl, CURLOPT_POSTFIELDS, send_data.data());
   curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, send_data.size());
   curl_easy_setopt(curl, CURLOPT_POST, true);
   curl_easy_setopt(curl, CURLOPT_URL, url);

   recv_data.clear();
   res = curl_easy_perform(curl);
   curl_easy_setopt(curl, CURLOPT_POST, false);
   return res;
}

// Result of trying to login should be checked by caller.
CURLcode basic_http::i_cookie_login(const char *cookie_file, const char *url)
{
   curl_easy_setopt(curl, CURLOPT_COOKIEJAR, cookie_file);
   curl_easy_setopt(curl, CURLOPT_COOKIEFILE, cookie_file);
   return i_post(url);
}

CURLcode basic_http::get(const char *url)
{
   recv_data.clear();
   curl_easy_setopt(curl, CURLOPT_URL, url);
   return curl_easy_perform(curl);
}

} // BASIC_CURL NAMESPACE
