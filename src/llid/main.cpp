#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>

#include <string>
#include <vector>

#include "typedef.h"
#include "buffer.h"
#include "aux_log.h"

namespace {
   enum snmp {
      iftype_len = 10,
      ifos_oidlen = 11,
      ifn_oidlen = 12,
      ifa_oidlen = 12,
      ifhs_oidlen = 12,
      MAX_REP = 60
   };

   enum interface_types {
      ethernetCsmacd = 6,
      gigabitEthernet = 117
   };
}

std::string escape_string(const char *string, size_t size)
{
   std::string result;
   std::size_t pos = std::string::npos;   

   result.append(string, size);
   while (std::string::npos != (pos = result.find('"', (pos == std::string::npos) ? 0 : pos + 2)))
      result.replace(pos, sizeof(char), "\\\"");
   return result;
}

snmp_session * init_snmp_session(const char *host, const char *community)
{
   static const char *funcname = "init_snmp_session";
   struct snmp_session sess;
   struct snmp_session *sessp = nullptr;

   snmp_sess_init(&sess);
   sess.version = SNMP_VERSION_2c;
   sess.peername = const_cast<char *>(host);
   sess.community = (u_char *) community;     // Why unsigned char?
   sess.community_len = strlen(community);

   if (nullptr == (sessp = snmp_open(&sess)))
   {
      int liberr, syserr;
      char *errstr;
      snmp_error(&sess, &liberr, &syserr, &errstr);
      logger.error_exit(funcname, "%s: Got error when called snmp_open() for host: %s", host, errstr);
   }
   return sessp;
}

std::vector<uint_t> get_host_interfaces(snmp_session *sessp)
{
   static const char *funcname = "get_host_interfaces";

   std::vector<uint_t> interfaces;
   oid ifType[iftype_len] = { 1, 3, 6, 1, 2, 1, 2, 2, 1, 3 };
   netsnmp_pdu *response = nullptr;
   netsnmp_pdu *request = snmp_pdu_create(SNMP_MSG_GETBULK);

   request->non_repeaters = 0;
   request->max_repetitions = MAX_REP;
   snmp_add_null_var(request, ifType, iftype_len);

   for (;;)
   {
      if (nullptr != response) snmp_free_pdu(response);
      if (STAT_SUCCESS == snmp_synch_response(sessp, request, &response) and
          SNMP_ERR_NOERROR == response->errstat)
      {
         for (netsnmp_variable_list *vars = response->variables; nullptr != vars; vars = vars->next_variable)
         {
            if (netsnmp_oid_is_subtree(ifType, iftype_len, vars->name, vars->name_length))
            {
               snmp_free_pdu(response);
               return interfaces;
            }

            if (ASN_INTEGER != vars->type) logger.error_exit(funcname, "Unexpected ASN type in answer to ifType: %s", sessp->peername);
            if (ethernetCsmacd == *(vars->val.integer) or gigabitEthernet == *(vars->val.integer))
               interfaces.push_back(*(vars->name + iftype_len));

            if (nullptr == vars->next_variable)
            {
               request = snmp_pdu_create(SNMP_MSG_GETBULK);
               request->non_repeaters = 0;
               request->max_repetitions = MAX_REP;
               snmp_add_null_var(request, vars->name, vars->name_length);
            }
         }
      } // in response

      else logger.error_exit(funcname, "Some snmp error when requesting ifType from host '%s'", sessp->peername);
   } // infinite loop
}

void get_interface_data(uint_t number, netsnmp_variable_list *vars, buffer &json_data)
{
   static const char *funcname = "parse_interface_data";

   for (int i = 0; nullptr != vars; vars = vars->next_variable, i++)
   {
      switch (i)
      {
         case 0:
            if (ASN_INTEGER != vars->type) throw logging::error(funcname, "Unexpected ASN type in answer to ifOperStatus");
            if (1 != *(vars->val.integer)) return;
            break;

         case 1:
            if (ASN_OCTET_STR != vars->type) throw logging::error(funcname, "Unexpected ASN type in answer to ifName");
            json_data.append("{\"{#IFINDEX}\":\"%lu\",\"{#IFNAME}\":\"%s\",", number, 
                  (escape_string((const char *) vars->val.string, vars->val_len)).c_str());
            break;

         case 2:
            if (ASN_OCTET_STR != vars->type) throw logging::error(funcname, "Unexpected ASN type in answer to ifAlias");
            json_data.append("\"{#IFALIAS}\":\"%s\",", (escape_string((const char *) vars->val.string, vars->val_len)).c_str());
            break;

         case 3:
            if ((ASN_APPLICATION | ASN_INTEGER) != vars->type) throw logging::error(funcname, "Unexpected ASN type in answer to ifHighSpeed");
            json_data.append("\"{#IFSPEED}\":\"%ld\"},", *(vars->val.integer));
            break;

         default: throw logging::error(funcname, "Unexpected step number while parsing host response");
      }
   }
}

buffer process_interfaces(snmp_session *sessp, const std::vector<uint_t> &interfaces)
{
   static const char *funcname = "process_interfaces";

   oid ifOperStatus[ifos_oidlen] = { 1, 3, 6, 1, 2, 1, 2, 2, 1, 8 };
   oid ifName[ifn_oidlen] = { 1, 3, 6, 1, 2, 1, 31, 1, 1, 1, 1 };
   oid ifAlias[ifa_oidlen] = { 1, 3, 6, 1, 2, 1, 31, 1, 1, 1, 18 };
   oid ifHighSpeed[ifhs_oidlen] = { 1, 3, 6, 1, 2, 1, 31, 1, 1, 1, 15 };   

   buffer json_data;
   netsnmp_pdu *response = nullptr;
   netsnmp_pdu *request;

   json_data.print("{\"data\":[");
   for (auto int_number : interfaces)
   {
      if (nullptr != response) snmp_free_pdu(response);
      request = snmp_pdu_create(SNMP_MSG_GET);

      ifOperStatus[ifos_oidlen - 1] = int_number;
      ifName[ifn_oidlen - 1] = int_number;
      ifAlias[ifa_oidlen - 1] = int_number;
      ifHighSpeed[ifhs_oidlen - 1] = int_number;

      snmp_add_null_var(request, ifOperStatus, ifos_oidlen);
      snmp_add_null_var(request, ifName, ifn_oidlen);
      snmp_add_null_var(request, ifAlias, ifa_oidlen);
      snmp_add_null_var(request, ifHighSpeed, ifhs_oidlen);

      if (STAT_SUCCESS == snmp_synch_response(sessp, request, &response) and
          SNMP_ERR_NOERROR == response->errstat)
      {
         try { get_interface_data(int_number, response->variables, json_data); }
         catch (logging::error &error) { logger.error_exit(funcname, "%s: %s", sessp->peername, error.what()); }
      }
      else logger.error_exit(funcname, "Some snmp error when requesting additional int info from host '%s'", sessp->peername);
   }

   json_data.pop_back();
   json_data.append("]}");
   return json_data;
}

int main(int argc, char *argv[])
{
   const char *progname = "llid";   
   openlog(progname, LOG_PID, LOG_LOCAL7);
   if (3 != argc) logger.error_exit(progname, "%s: should be called with exactly two arguments: [ip] [snmp_community]", argv[0]);
   if (0 == strcmp("127.0.0.1", argv[1])) return 0;

   snmp_session *sessp = init_snmp_session(argv[1], argv[2]);
   std::vector<uint_t> interfaces = get_host_interfaces(sessp);
   buffer json_data = process_interfaces(sessp, interfaces);
   printf(json_data.data());

   snmp_close(sessp);
   return 0;
}
