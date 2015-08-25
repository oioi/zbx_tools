#ifndef ZBX_API_H
#define ZBX_API_H

#include "curl_cl.h"
#include "frozen.h"
#include "typedef.h"

namespace zbx_api {

namespace item {

   enum class type : unsigned int {
      zbx_agent,
      snmpv1_agent,
      zbx_trapper,
      simple_check,
      snmpv2_agent,
      zabbix_internal,
      snmpv3_agent,
      zbx_agent_active,
      zbx_aggregate,
      web_item,
      external_check,
      db_monitor,
      ipmi_agent,
      ssh_agent,
      telnet_agent,
      calculated,
      jmx_agent,
      snmp_trap
   };

   enum class value_type : unsigned int {
      float_t,
      char_t,
      log_t,
      unsigned_t,
      text_t
   };

} // ITEM NAMESPACE

enum class host_interface_type {
   zbx_agent = 1,
   zbx_snmp,
   zbx_ipmi,
   zbx_jmx
};

enum class trigger_severity : unsigned int {
   not_classified,
   information,
   warning,
   average,
   high,
   disaster
};

class api_session
{
   public:
      // Received data from server will be parsed and stored here, so it must be accessible.
      json_token *arr;
      json_token *tok;

      api_session() : arr(nullptr), active(false), req_id(1) { }
      ~api_session() noexcept { if (nullptr != arr) free(arr); }

      void set_auth(const std::string &i_url, const std::string &i_user, const std::string &i_password);

      bool json_get_uint(const char *json_path, uint_t *result);
      bool json_get_str(const char *json_path, buffer *buf);
      uint_t json_desc_num(const char *json_path);

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

uint_t create_group(const std::string &name, api_session &zbx_sess);
uint_t get_groupid_byname(const std::string &name, api_session &zbx_sess);
uint_t get_templateid_byname(const std::string &name, api_session &zbx_sess);

} // ZBX_API NAMESPACE

#endif
