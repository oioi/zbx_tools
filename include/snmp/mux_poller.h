#ifndef SNMP_MUXPOLLER_H
#define SNMP_MUXPOLLER_H

#include <list>
#include "snmp/snmp.h"

namespace snmp {

using callback_wf = int (*) (int, struct snmp_session *, int, struct snmp_pdu *, void *, void *);

enum callback_retcodes : int {
   ok,
   ok_close
};

enum class pollstate {
   polling,
   finished
};

struct polldata
{
   polldata(void *sessp_ = nullptr) : sessp{sessp_}, state{pollstate::polling} { }
   ~polldata() { if (nullptr != sessp) snmp_sess_close(sessp); }

   polldata(polldata &&other) : sessp{other.sessp}, state{other.state} { other.sessp = nullptr; }
   polldata(polldata &other) = delete;
   polldata & operator=(polldata &other) = delete;
   polldata & operator=(polldata &&other) = delete;

   void *sessp;
   pollstate state;
};

struct polltask
{
   polltask(const char *host_, const char *community_, netsnmp_pdu *request_,
            callback_wf callback_, void *magic_, long version_) :
      host{host_}, community{community_}, request{request_},
      callback{callback_}, magic{magic_}, version{version_} { }
   ~polltask() { if (nullptr != request) snmp_free_pdu(request); }   

   polltask(polltask &&other);
   polltask(polltask &other) = delete;
   polltask & operator =(polltask &other) = delete;
   polltask & operator =(polltask &&other) = delete;   

   std::string host;
   std::string community;

   netsnmp_pdu *request;
   callback_wf callback;
   polldata *pdata;

   void *magic;
   long version;
};

class mux_poller
{
   public:
      mux_poller(unsigned max_hosts_ = 512) : max_hosts{max_hosts_} { }

      void clear() { tasks.clear(); }
      void add(const char *host, const char *community, netsnmp_pdu *request,
               callback_wf callback, void *magic = nullptr, long version = default_version) {
         tasks.emplace_back(host, community, request, callback, magic, version); }

      void poll();

   private:
      unsigned max_hosts;
      std::vector<polltask> tasks;
      std::list<polldata> sessions;
};

} // NAMESPACE END

#endif
