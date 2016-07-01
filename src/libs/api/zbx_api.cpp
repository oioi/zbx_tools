#include <stdlib.h>

#include "typedef.h"
#include "aux_log.h"
#include "zbx_api.h"

namespace zbx_api {

int api_session::send_json(const char *send_buffer)
{
   static const char *funcname = "api_session::send_json";
   CURLcode res;

   if (nullptr != send_buffer)
      api_conn.send_data.print(R"**({"jsonrpc":"2.0",%s,"id":%lu,"auth":"%s"})**", 
            send_buffer, req_id, auth_token.get());

   if (CURLE_OK != (res = api_conn.post()))
      throw logging::error(funcname, "CURL failed: %s", curl_easy_strerror(res));
   req_id++;

   if (nullptr != arr) free(arr);
   arr = parse_json2(api_conn.recv_data.data(), api_conn.recv_data.size());

   if (nullptr != (tok = find_json_token(arr, "error.code")))
   {
      json_token *message = find_json_token(arr, "error.message");
      json_token *data = find_json_token(arr, "error.data");

      throw logging::error(funcname, "Received ERROR response: %.*s - %.*s - %.*s after sending: '%s'", tok->len, tok->ptr,
                 message->len, message->ptr, data->len, data->ptr, api_conn.send_data.data());
   }

   tok = find_json_token(arr, "result");
   return tok->num_desc;
}

int api_session::send_vstr(const char *format, ...)
{
   if (!active) init();
   va_list args;
   va_start(args, format);

   api_conn.send_data.print(R"**({"jsonrpc":"2.0",)**");
   api_conn.send_data.vappend(format, args);
   api_conn.send_data.append(R"**(,"id":%lu,"auth":"%s"})**", req_id, auth_token.get());

   va_end(args);
   return send_json(nullptr);
}

int api_session::send_plain(const char *send_buffer)
{
   if (!active) init();
   return send_json(send_buffer);
}

void api_session::set_auth(const std::string &i_url, const std::string &i_user, const std::string &i_password)
{
   url = i_url;
   username = i_user;
   password = i_password;
}

void api_session::init()
{
   static const char *funcname = "api_session::init";

   api_conn.add_header("Content-Type: application/json");
   api_conn.set_default_url(url);

   api_conn.send_data.print(R"**({"jsonrpc":"2.0","method":"user.login","params":{"user":"%s","password":"%s"},"id":%lu})**",
                            username.c_str(), password.c_str(), req_id);
   send_json(nullptr);

   if (nullptr == (tok = find_json_token(arr, "result")))
      throw logging::error(funcname, "Unexpected answer after authentication. Source string: %s", api_conn.recv_data.data());

   char *token = new char[tok->len + 1];
   sprintf(token, "%.*s", tok->len, tok->ptr);
   auth_token.reset(token);
   active = true;
}

bool api_session::json_get_uint(const char *json_path, uint_t *result)
{
   if (nullptr == (tok = find_json_token(arr, json_path))) return false;
   *result = strtoul(tok->ptr, nullptr, 10);
   return true;
}

uint_t api_session::json_desc_num(const char *json_path)
{
   if (nullptr == (tok = find_json_token(arr, json_path))) return 0;
   return tok->num_desc;
}

bool api_session::json_get_str(const char *json_path, buffer *buf)
{
   if (nullptr == (tok = find_json_token(arr, json_path))) return false;
   buf->print("%.*s", tok->len, tok->ptr);
   return true;
}

std::wstring parse_codestring(const std::string &data)
{
   std::wstring result;
   std::string token;
   wchar_t symbol;

   size_t len = data.size();
   for (unsigned i = 0; i < len; )
   {
      if ('\\' == data[i])
      {
         // Theoretically we can go out of string bounds, but who cares.
         if ('u' == data[i + 1])
         {
            token.clear();
            token.append(data.c_str() + i + 2, 4);
            symbol = std::stoul(token, nullptr, 16);
            result.push_back(symbol);
            i += 6;
         }
         else i++;
      }

      for (; i < len and '\\' != data[i]; i++)
      {
         mbstowcs(&symbol, &data[i], 1);
         result.push_back(symbol);         
      }
   }

   return result;
}

uint_t get_groupid_byname(const std::string &name, api_session &zbx_sess)
{
   static const char *funcname = "zbx_api::get_groupid_byname";

   if (0 == zbx_sess.send_vstr(R"**(
      "method": "hostgroup.get",
      "params": {
         "output": "groupid",
         "filter": { "name": [ "%s" ] } }
   )**", name.c_str())) return 0;

   uint_t temp;
   if (false == zbx_sess.json_get_uint("result[0].groupid", &temp))
      throw logging::error(funcname, "Cannot get group ID from JSON response");
   return temp;
}

uint_t create_group(const std::string &name, api_session &zbx_sess)
{
   static const char *funcname = "zbx_api::create_group";

   zbx_sess.send_vstr(R"**(
      "method": "hostgroup.create",
      "params": { "name": "%s" }
   )**", name.c_str());

   uint_t temp;
   if (false == zbx_sess.json_get_uint("result.groupids[0]", &temp))
      throw logging::error(funcname, "Cannot get created group ID from JSON response");
   return temp;
}

uint_t get_templateid_byname(const std::string &name, api_session &zbx_sess)
{
   static const char *funcname = "zbx_api::get_templateid_byname";

   if (0 == zbx_sess.send_vstr(R"**(
      "method": "template.get",
      "params": {
         "output": "name",
         "filter": { "host": [ "%s" ] } }
   )**", name.c_str())) return 0;

   uint_t temp;
   if (false == zbx_sess.json_get_uint("result[0].templateid", &temp))
      throw logging::error(funcname, "Cannot get template ID from JSON response");
   return temp;
}



} // ZBX_API NAMESPACE
