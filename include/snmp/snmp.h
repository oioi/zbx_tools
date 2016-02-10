#ifndef COMMON_SNMP_H
#define COMMON_SNMP_H

#include <vector>

#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>

#include "aux_log.h"

namespace snmp {

namespace oids {
   extern const oid objid[9];
   extern const oid tticks[9];

   extern const oid if_broadcast[12];
}

enum iana_iftypes {
   ethernetCsmacd = 6,
   gigabitEthernet = 117
};

enum {
   default_version = SNMP_VERSION_2c
};

enum class errtype {
   timeout,             // Timeout on request
   invalid_input,       // Invalid input data
   invalid_data,        // Invalid or unexpected response data in PDU
   snmp_error,          // Errors in SNMP packet
   runtime              // Generic runtime error
};

struct snmprun_error : public std::exception
{
   errtype type;
   buffer message;

   const char *what() const noexcept { return message.data(); }
   snmprun_error(errtype type, const char *funcname, const char *format, ...) noexcept
      __attribute__((format(printf,4,5)));
   ~snmprun_error() noexcept { }
};

// Exceptions for runtime errors inside NET-SNMP library itself.
struct snmplib_error : public std::exception
{
   buffer message;

   const char *what() const noexcept { return message.data(); }
   snmplib_error(const char *funcname, const char *format, ...) noexcept
      __attribute__((format(printf,3,4)));
   ~snmplib_error() noexcept { }
};

struct sess_handle
{
   void *ptr;

   sess_handle(void *ptr_ = nullptr) : ptr{ptr_} { }
   void close() { if (nullptr != ptr) { snmp_sess_close(ptr); ptr = nullptr; } }   
   ~sess_handle() { close(); }

   operator void *() { return ptr; }   
   sess_handle & operator =(void *ptr_)
   {
      if (ptr != ptr_) { if (nullptr != ptr) snmp_sess_close(ptr); ptr = ptr_; }
      return *this;
   }
};

struct pdu_handle
{
   netsnmp_pdu *pdu;

   pdu_handle(netsnmp_pdu *pdu_ = nullptr) : pdu{pdu_} { }
   void free() { if (nullptr != pdu) { snmp_free_pdu(pdu); pdu = nullptr; } }
   ~pdu_handle() { free(); }

   operator netsnmp_pdu *() { return pdu; }
   pdu_handle & operator =(netsnmp_pdu *pdu_)
   {
      if (pdu != pdu_) { if (nullptr != pdu) snmp_free_pdu(pdu); pdu = pdu_; }
      return *this;
   }
};

// Basic facilities

using callback_f = int (*) (int, struct snmp_session *, int, struct snmp_pdu *, void *);
void * init_snmp_session(const char *host, const char *community, long version = default_version,
                         callback_f callback = nullptr, void *magic = nullptr);

netsnmp_pdu * synch_request(void *sessp, netsnmp_pdu *request);
void async_send(void *sessp, netsnmp_pdu *request);

std::string print_objid(netsnmp_variable_list *vars);

// Specific, but used from time to time

using intdata = std::vector<unsigned>;

std::string get_host_objid(void *sessp);
intdata get_host_physints(void *sessp);

struct int_info_st
{
   unsigned id;
   bool active;
   std::string name;
   std::string alias;
   unsigned speed;

   int_info_st() : active{false}, speed{} { }
};

using intinfo = std::vector<int_info_st>;
intinfo get_intinfo(void *sessp, intdata &ints);

}

#endif
