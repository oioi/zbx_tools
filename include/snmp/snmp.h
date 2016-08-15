#ifndef SNMPLIB_H
#define SNMPLIB_H

#include <initializer_list>
#include <string>
#include <vector>

#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>

#include "aux_log.h"
#include "snmp/oids.h"

namespace snmp {

// Some of interface types defined by IANA (ifType MIB)
enum iana_iftypes {
   ethernetCsmacd = 6,
   gigabitEthernet = 117
};

enum {
   default_version = SNMP_VERSION_2c,
   default_pdu_type = SNMP_MSG_GET,
   default_bulk_repetitions = 0,
   default_bulk_maxoids = 60
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

// Basic RAII SNMP session handler
struct sess_handle
{
   void *ptr;

   sess_handle(void *ptr_ = nullptr) : ptr{ptr_} { }
   void close() { if (nullptr != ptr) { snmp_sess_close(ptr); ptr = nullptr; } }
   ~sess_handle() { close(); }

   sess_handle(const sess_handle &other) = delete;
   sess_handle(sess_handle &&other) : ptr{other.ptr} { other.ptr = nullptr; }

   sess_handle & operator =(const sess_handle &other) = delete;
   sess_handle & operator =(sess_handle &&other) { ptr = other.ptr; other.ptr = nullptr; return *this; }

   operator void *() { return ptr; }
   sess_handle & operator =(void *ptr_)
   {
      if (ptr != ptr_) { if (nullptr != ptr) snmp_sess_close(ptr); ptr = ptr_; }
      return *this;
   }
};

// Same RAII for SNMP PDU
struct pdu_handle
{
   netsnmp_pdu *pdu;

   pdu_handle(netsnmp_pdu *pdu_ = nullptr) : pdu{pdu_} { }
   void free() { if (nullptr != pdu) { snmp_free_pdu(pdu); pdu = nullptr; } }
   ~pdu_handle() { free(); }

   pdu_handle(const pdu_handle &other) { pdu = (nullptr == other.pdu) ? nullptr : snmp_clone_pdu(other.pdu); }
   pdu_handle(pdu_handle &&other) : pdu{other.pdu} { other.pdu = nullptr; }

   pdu_handle & operator =(const pdu_handle &other) { pdu = (nullptr == other.pdu) ? nullptr : snmp_clone_pdu(other.pdu); return *this; }
   pdu_handle & operator =(pdu_handle &&other) { pdu = other.pdu; other.pdu = nullptr; return *this; }

   operator netsnmp_pdu *() { return pdu; }
   pdu_handle & operator =(netsnmp_pdu *pdu_)
   {
      if (pdu != pdu_) { if (nullptr != pdu) snmp_free_pdu(pdu); pdu = pdu_; }
      return *this;
   }
};

class oid_handle
{
   public:
      oid_handle(const std::initializer_list<oid> &source) : size_ {source.size()} 
      {
         data_.reset(new oid[size_]);
         oid *ptr = data_.get();
         for (auto &it : source) { *ptr++ = it; }
      }

      oid_handle(const oid *source, size_t size) { copy(source, size); }
      oid_handle(oid_handle &&other) { move(other); }

      oid_handle & operator =(const oid_handle &other) { copy(other.data_.get(), other.size_); return *this; }
      oid_handle & operator =(oid_handle &&other) { move(other); return *this; }

      operator oid *() { return data_.get(); };
      oid & operator [](unsigned i) { return data_[i]; }
      size_t size() const { return size_; }

   private:
      size_t size_;
      std::unique_ptr<oid []> data_;

      void copy(const oid *source, size_t size)
      {
         size_ = size;
         data_.reset(new oid[size]);
         memcpy(data_.get(), source, size * sizeof(oid));
      }

      void move(oid_handle &other)
      {
         size_ = other.size_;
         data_ = std::move(other.data_);
         other.size_ = 0;
      }
};

// Basic facilites

using callback_f = int (*) (int, struct snmp_session *, int, struct snmp_pdu *, void *);
void * init_snmp_session(const char *host, const char *community, long version = default_version,
                         callback_f callback = nullptr, void *magic = nullptr);

netsnmp_pdu * synch_request(void *sessp, netsnmp_pdu *request);
netsnmp_pdu * synch_request(void *sessp, const oid *reqoid, size_t oidsize, int type = default_pdu_type,
                            int rep = default_bulk_repetitions, int max = default_bulk_maxoids);
void async_send(void *sessp, netsnmp_pdu *request);

std::string print_objid(netsnmp_variable_list *var);

// Specific, but used from time to time

std::string print_oid(const oid *oid, size_t oidsize);
std::string print_objid(netsnmp_variable_list *var);
std::string get_host_objid(void *sessp);

std::vector<unsigned> get_nodes_bytype(void *sessp, const oid *oidst, size_t oidsize, const std::vector<unsigned> &match);

using intdata = std::vector<unsigned>;
inline intdata get_host_physints(void *sessp) {
   return get_nodes_bytype(sessp, oids::iftype, oids::iftype_size - 1, { ethernetCsmacd, gigabitEthernet }); }

struct int_info_st
{
   unsigned id;
   bool active;
   std::string name;
   std::string alias;
   unsigned speed;

   int_info_st(unsigned id_ = 0) : id{id_}, active{false}, speed{} { }
};

using intinfo = std::vector<int_info_st>;
intinfo get_intinfo(void *sessp, const intdata &ints);

} // SNMP NAMESPACE

#endif
