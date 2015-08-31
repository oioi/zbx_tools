#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>

#include <vector>

#include "aux_log.h"
#include "prog_config.h"
#include "main.h"

int snmp_get_objid(glob_hostdata &hostdata, long snmp_version)
{
   static const char *funcname = "snmp_get_objid";
   static const oid objID[] = { 1, 3, 6, 1, 2, 1, 1, 2, 0 };

   int liberr, syserr, status;
   char *errstr;   

   snmp_session snmp_sess;
   snmp_session *sessp = nullptr;
   netsnmp_pdu *request = nullptr, *response = nullptr;

   const char *hoststr = hostdata.host.c_str();
   const conf::multistring_t &comms = config["snmp_communities"].get<conf::multistring_t>();   

   for (unsigned int i = 0; i < comms.size(); i++)
   {
      if (nullptr != response) snmp_free_pdu(response);
      if (nullptr != sessp) snmp_close(sessp);

      snmp_sess_init(&snmp_sess);
      snmp_sess.version = snmp_version;
      snmp_sess.peername = const_cast<char *>(hoststr);
      snmp_sess.community = reinterpret_cast<u_char *>(const_cast<char *>(comms[i].c_str())); // Hello, netsnmp!
      snmp_sess.community_len = comms[i].size();

      if (nullptr == (sessp = snmp_open(&snmp_sess)))
      {
         snmp_error(&snmp_sess, &liberr, &syserr, &errstr);
         logger.error_exit(funcname, "%s: Got error when called snmp_open() for host: %s", hoststr, errstr);
      }

      request = snmp_pdu_create(SNMP_MSG_GET);
      snmp_add_null_var(request, objID, sizeof(objID) / sizeof(oid));

      if (STAT_SUCCESS == (status = snmp_synch_response(sessp, request, &response)) and
          SNMP_ERR_NOERROR == response->errstat)
      {
         netsnmp_variable_list *vars = response->variables;
         if (SNMP_NOSUCHOBJECT == vars->type) 
         {
            logger.log_message(LOG_INFO, funcname, 
                  "%s: host returned 'no such object' for objID OID. Assuming, that it is a dumb piece of can.", hoststr);
            return 0;
         }         
         if (ASN_OBJECT_ID != vars->type) logger.error_exit(funcname, "%s: Device returned an unexpected ASN type for ObjID OID.", hoststr);

         char *objid_str = new char[max_objid_strlen];
         int len;

         if (-1 == (len = snprint_objid(objid_str, max_objid_strlen, vars->val.objid, vars->val_len / sizeof(oid))))
            logger.error_exit(funcname, "snprint_objid failed. buffer is not large enough?");
         objid_str[len] = '\0';

         hostdata.objid.setmem(objid_str, max_objid_strlen, len);
         hostdata.community = comms[i];

         snmp_free_pdu(response);
         snmp_close(sessp);
         return 1;
      }

      else if (STAT_SUCCESS == status)
      {
         switch (response->errstat)
         {
            case SNMP_ERR_NOACCESS:
               logger.log_message(LOG_INFO, funcname, "%s: returned ERR_NOACCESS with version %lu and community '%s'",
                     hoststr, snmp_version, snmp_sess.community); 
               break;

            case SNMP_ERR_NOSUCHNAME:
               logger.log_message(LOG_INFO, funcname, "%s: returned SNMP_ERR_NOSUCHNAME. Dumb piece of can?", hoststr); 
               break;

            default:
               logger.log_message(LOG_CRIT, funcname, "%s: unexpected SNMP status in packet: %lu", hoststr, response->errstat);
         }
         continue;
      }
      else if (STAT_TIMEOUT == status) continue;

      snmp_error(&snmp_sess, &liberr, &syserr, &errstr);
      logger.error_exit(funcname, "%s: error while sending GET request: %s", hoststr, errstr);
   }

   snmp_free_pdu(response);
   snmp_close(sessp);
   return 0;
}
