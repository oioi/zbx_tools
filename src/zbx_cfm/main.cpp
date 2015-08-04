#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>

#include <algorithm>
#include <vector>

#include "aux_log.h"
#include "prog_config.h"
#include "zbx_api.h"
#include "main.h"

static configuration::config_map zabbix_section = {
   { "api-url",       { true, static_cast<char *>(nullptr) } },   // Obviously for API
   { "username",      { true, static_cast<char *>(nullptr) } },
   { "password",      { true, static_cast<char *>(nullptr) } }
};

configuration::config_map config = {
   { "snmp-community", { true, static_cast<char *>(nullptr) } },
   { "zabbix",         { true, &zabbix_section } }
};

namespace {
   const char *progname = "zbx_cfm";
   const char *conffile = "zbx_cfm.conf";
   zbx_api::api_session zbx_sess;
}

void snmp_get_objid(hostdata &host)
{
   static const char *funcname = "snmp_get_objid";
   static oid objID[] = { 1, 3, 6, 1, 2, 1, 1, 2, 0 };

   struct snmp_session snmp_sess;
   struct snmp_session *sessp = nullptr;

   snmp_sess_init(&snmp_sess);
   snmp_sess.version = SNMP_VERSION_2c;
   snmp_sess.peername = const_cast<char *>(host.hostname.c_str());

   // Fuck that shit, net-snmp
   snmp_sess.community = (u_char *) config["snmp-community"].str().c_str();
   snmp_sess.community_len = config["snmp-community"].str().size();

   if (nullptr == (sessp = snmp_open(&snmp_sess)))
   {
      int liberr, syserr;
      char *errstr;
      snmp_error(&snmp_sess, &liberr, &syserr, &errstr);
      logger.error_exit(funcname, "%s: Got error when called snmp_open() for host: %s", host.hostname.c_str(), errstr);
   }

   netsnmp_pdu *response = nullptr;
   netsnmp_pdu *request = snmp_pdu_create(SNMP_MSG_GET);
   snmp_add_null_var(request, objID, sizeof(objID) / sizeof(oid));

   if (STAT_SUCCESS == snmp_synch_response(sessp, request, &response) and
       SNMP_ERR_NOERROR == response->errstat)
   {
      netsnmp_variable_list *vars = response->variables;
      if (SNMP_NOSUCHOBJECT == vars->type)
         logger.error_exit(funcname, "%s: host returned 'no such object' for objID OID", host.hostname.c_str());

      if (ASN_OBJECT_ID != vars->type) logger.error_exit(funcname, "%s: Device returned an unexpected ASN type for ObjID OID.", host.hostname.c_str());

      char *objid_str = new char[max_objid_strlen];
      int len;

      if (-1 == (len = snprint_objid(objid_str, max_objid_strlen, vars->val.objid, vars->val_len / sizeof(oid))))
         throw logging::error(funcname, "snprint_objid failed. buffer is not large enough?");
      objid_str[len] = '\0';
      host.objid.setmem(objid_str, max_objid_strlen, len);

      snmp_free_pdu(response);
      snmp_close(sessp);
      return;
   }

   logger.error_exit(funcname, "Timeout or error status while getting objID from '%s'", host.hostname.c_str());
}

int main(int argc, char *argv[])
{
   logger.method = logging::log_method::M_STDO;
   if (argc < 3) logger.error_exit(progname, "Wrong argument count.\nUsage: zbx_cfm [add | delete] vlan ips ...");

   std::string action = argv[1];
   if ("add" != action and "del" != action)
      logger.error_exit(progname, "Wrong action: %s", action.c_str());

   if (0 == configuration::read_config(conffile, config))
      logger.error_exit(progname, "Error while reading configuration file.");

   std::vector<hostdata> hosts;
   for (int i = 3; i < argc; i++) hosts.push_back(hostdata(argv[i]));

   // Since some of zabbix devices can be added manually without discovering, 
   // we have to actually poll devices to determine device type.
   init_snmp(progname);
   netsnmp_ds_set_int(NETSNMP_DS_LIBRARY_ID, NETSNMP_DS_LIB_OID_OUTPUT_FORMAT, NETSNMP_OID_OUTPUT_NUMERIC);

   for (auto &host : hosts)
   {
      snmp_get_objid(host);




   }



   curl_global_cleanup();
   return 0;
}
