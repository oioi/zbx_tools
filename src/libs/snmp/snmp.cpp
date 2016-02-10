#include "snmp/snmp.h"

namespace snmp {

 namespace oids {
    const oid objid[] = { 1, 3, 6, 1, 2, 1, 1, 2, 0 };
    const oid tticks[] = { 1, 3, 6, 1, 2, 1, 1, 3, 0 };

    const oid if_broadcast[] = { 1, 3, 6, 1, 2, 1, 31, 1, 1, 1, 9, 0 };
 }

snmprun_error::snmprun_error(errtype type_, const char *funcname, const char *format, ...) noexcept :
   type{type_}
{
   va_list args;
   va_start(args, format);
   logging::default_errstr(message, -1, funcname, format, args);
   va_end(args);
}

snmplib_error::snmplib_error(const char *funcname, const char *format, ...) noexcept
{
   va_list args;
   va_start(args, format);
   logging::default_errstr(message, -1, funcname, format, args);
   va_end(args);
}

void * init_snmp_session(const char *host, const char *community, long version, callback_f callback, void *magic)
{
   static const char *funcname {"snmp::init_snmp_session"};
   struct snmp_session sess;

   snmp_sess_init(&sess);
   sess.version = version;
   sess.peername = const_cast<char *>(host);
   sess.community = reinterpret_cast<u_char *>(const_cast<char *>(community));
   sess.community_len = strlen(community);

   if (callback)
   {
      sess.callback = callback;
      sess.callback_magic = magic;
   }

   void *sessp = snmp_sess_open(&sess);
   if (nullptr == sessp)
   {
      int liberr, syserr;
      char *errstr;

      snmp_error(&sess, &liberr, &syserr, &errstr);
      std::string errmsg {errstr};
      free(errstr);
      throw snmplib_error {funcname, "%s: got error when callen snmp_sess_open() for host: %s", host, errstr};
   }

   return sessp;
}

void async_send(void *sessp, netsnmp_pdu *request)
{
   static const char *funcname {"snmp::async_send"};

   if (nullptr == sessp)
      throw snmprun_error {errtype::invalid_input, funcname, "session nullptr"};
   if (nullptr == request)
      throw snmprun_error {errtype::invalid_input, funcname, "request nullptr"};

   if (0 == snmp_sess_send(sessp, request))
   {
      int liberr, syserr;
      char *errstr;

      snmp_free_pdu(request);
      snmp_sess_error(sessp, &liberr, &syserr, &errstr);
      std::string errmsg {errstr};
      free(errstr);

      throw snmplib_error {funcname, "snmp_sess_send failed: %s", errmsg.c_str()};
   }
}

netsnmp_pdu * synch_request(void *sessp, netsnmp_pdu *request)
{
   static const char *funcname {"snmp::synch_request"};
   netsnmp_pdu *response;

   int status = snmp_sess_synch_response(sessp, request, &response);
   if (STAT_TIMEOUT == status) throw snmprun_error {errtype::timeout, funcname, "request timeout"};

   // NOTE: Should this be a lib error?
   if (STAT_SUCCESS != status)
      throw snmprun_error {errtype::runtime, funcname, "failed to perform request."};

   switch (response->errstat)
   {
      case SNMP_ERR_NOERROR: 
         return response;

      default:
         // NOTE: Maybe we should just return PDU and let handle any errors in the level above?
         unsigned long code = response->errstat;
         snmp_free_pdu(response);
         throw snmprun_error {errtype::snmp_error, funcname, "error SNMP status in packet: %lu", code};
   }
}

netsnmp_pdu * synch_request(void *sessp, const oid *reqoid, size_t oidsize, int type = SNMP_MSG_GET, int rep = 0, int max = 60)
{
   netsnmp_pdu *request = snmp_pdu_create(type);

   if (SNMP_MSG_GETBULK == type)
   {
      request->non_repeaters = rep;
      request->max_repetitions = max;
   }

   snmp_add_null_var(request, reqoid, oidsize);
   return synch_request(sessp, request);
}

std::string print_objid(netsnmp_variable_list *vars)
{
   static const char *funcname {"snmp::print_objid"};
   static const int bufsize = 128;
   static char buffer[bufsize];

   if (ASN_OBJECT_ID != vars->type)
      throw snmprun_error {errtype::invalid_data, funcname, "host returned unexpected ASN type in answer to objid"};

   int len = snprint_objid(buffer, bufsize, vars->val.objid, vars->val_len / sizeof(oid));
   if (-1 == len) throw snmprun_error {errtype::runtime, funcname, "snprint_objid failed. buffer is not large enough?"};
   buffer[len] = '\0';

   return std::string {buffer};
}

std::string get_host_objid(void *sessp)
{
   pdu_handle response;
   response = synch_request(sessp, oids::objid, sizeof(oids::objid) / sizeof(oid));
   return print_objid(response.pdu->variables);
}

intdata get_host_physints(void *sessp)
{
   static const char *funcname {"snmp::get_host_physints"};
   static const oid iftype[] = { 1, 3, 6, 1, 2, 1, 2, 2, 1, 3 };

   const oid *oidst = iftype;
   size_t oidsize = sizeof(iftype) / sizeof(oid);
   std::vector<unsigned> ints;
   pdu_handle response;

   for (netsnmp_variable_list *vars;;)
   {
      response = synch_request(sessp, oidst, oidsize, SNMP_MSG_GETBULK);
      for (vars = response.pdu->variables; nullptr != vars; vars = vars->next_variable)
      {
         if (netsnmp_oid_is_subtree(iftype, sizeof(iftype) / sizeof(oid), vars->name, vars->name_length))
            return ints;

         if (ASN_INTEGER != vars->type)
            throw snmprun_error {errtype::invalid_data, funcname, "unexpected ASN type in asnwer to iftype"};
         if (ethernetCsmacd == *(vars->val.integer) or gigabitEthernet == *(vars->val.integer))
            ints.push_back(*(vars->name + 10));

         if (nullptr == vars->next_variable)
         {
            oidst = vars->name;
            oidsize = vars->name_length;
         }
      }
   }
}

int_info_st parse_intinfo(netsnmp_variable_list *vars, unsigned id)
{
   static const char *funcname {"snmp::parse_intinfo"};
   int_info_st info;
   info.id = id;

   for (int i = 0; nullptr != vars; vars = vars->next_variable, ++i)
   {
      switch (i)
      {
         case 0:
            if (ASN_INTEGER != vars->type)
               throw snmprun_error {errtype::invalid_data, funcname, "unexpected ASN type in aswer ot ifoperstatus"};
            if (1 == *(vars->val.integer)) info.active = true;
            break;

         case 1:
            if (ASN_OCTET_STR != vars->type)
               throw snmprun_error {errtype::invalid_data, funcname, "unexpected ASN type in answer to ifname"};
            info.name.append(reinterpret_cast<char *>(vars->val.string), vars->val_len);
            break;

         case 2:
            if (ASN_OCTET_STR != vars->type)
               throw snmprun_error {errtype::invalid_data, funcname, "unexpected ASN type in answer to ifalias"};
            info.alias.append(reinterpret_cast<char *>(vars->val.string), vars->val_len);
            break;

         case 3:
            if ((ASN_APPLICATION | ASN_INTEGER) != vars->type)
               throw snmprun_error {errtype::invalid_data, funcname, "unexpected ASN type in answer to ifhighspeed"};
            info.speed = *(vars->val.integer);
            break;

         default: throw snmprun_error {errtype::invalid_data, funcname, "unexpected step number while parsing interface data"};
      }
   }

   return info;
}

intinfo get_intinfo(void *sessp, intdata &ints)
{
   enum {
      ifoslen = 11,
      ifnlen = 12,
      ifalen = 12,
      ifhslen = 12
   };

   static oid ifoperstatus[] = { 1, 3, 6, 1, 2, 1, 2, 2, 1, 8, 0 };
   static oid ifname[] = { 1, 3, 6, 1, 2, 1, 31, 1, 1, 1, 1, 0 };
   static oid ifalias[] = { 1, 3, 6, 1, 2, 1, 31, 1, 1, 1, 18, 0 };
   static oid ifhighspeed[] = { 1, 3, 6, 1, 2, 1, 31, 1, 1, 1, 15, 0 };

   pdu_handle response;
   netsnmp_pdu *request;
   intinfo info;

   for (auto id : ints)
   {
      request = snmp_pdu_create(SNMP_MSG_GET);
      ifoperstatus[ifoslen - 1] = id;
      ifname[ifnlen - 1] = id;
      ifalias[ifalen - 1] = id;
      ifhighspeed[ifhslen - 1] = id;

      snmp_add_null_var(request, ifoperstatus, ifoslen);
      snmp_add_null_var(request, ifname, ifnlen);
      snmp_add_null_var(request, ifalias, ifalen);
      snmp_add_null_var(request, ifhighspeed, ifhslen);

      response = synch_request(sessp, request);
      info.push_back(parse_intinfo(response.pdu->variables, id));
   }

   return info;
}

}
