#ifndef ZBX_API_H
#define ZBX_API_H

#include "curl_cl.h"
#include "frozen.h"
#include "typedef.h"

namespace zbx_api {

namespace item {

   enum class type : unsigned int {
      ZBX_AGENT,
      SNMPV1_AGENT,
      ZBX_TRAPPER,
      SIMPLE_CHECK,
      SNMPV2_AGENT,
      ZABBIX_INTERNAL,
      SNMPV3_AGENT,
      ZBX_AGENT_ACTIVE,
      ZBX_AGGREGATE,
      WEB_ITEM,
      EXTERNAL_CHECK,
      DB_MONITOR,
      IPMI_AGENT,
      SSH_AGENT,
      TELNET_AGENT,
      CALCULATED,
      JMX_AGENT,
      SNMP_TRAP
   };

   enum class value_type : unsigned int {
      FLOAT,
      CHAR,
      LOG,
      UNSIGNED,
      TEXT
   };

} // ITEM NAMESPACE

enum class host_interface_type {
   ZBX_AGENT = 1,
   ZBX_SNMP,
   ZBX_IPMI,
   ZBX_JMX
};

enum class trigger_severity : unsigned int {
   NOT_CLASSIFIED,
   INFORMATION,
   WARNING,
   AVERAGE,
   HIGH,
   DISASTER
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

} // ZBX_API NAMESPACE

#endif
