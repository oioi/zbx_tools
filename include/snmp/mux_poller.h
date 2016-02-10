#ifndef SNMP_MUXPOLLER_H
#define SNMP_MUXPOLLER_H

#include <list>
#include <unordered_map>
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

// Used for active hosts. Holding current state and active SNMP session.
struct polldata
{
   polldata(void *sessp_ = nullptr) : sessp{sessp_}, state{pollstate::polling} { }
   ~polldata() { if (nullptr != sessp) snmp_sess_close(sessp); }

   polldata(const polldata &other) = delete;
   polldata & operator =(const polldata &other) = delete;

   polldata(polldata &&other) : sessp{other.sessp}, state{other.state} { other.sessp = nullptr; }
   polldata & operator =(polldata &&other) = delete;

   void *sessp;
   pollstate state;
};

struct polltask
{
   polltask(const char *community_, netsnmp_pdu *request_,
         callback_wf callback_, void *magic_, long version_) :
      community{community_}, request{snmp_clone_pdu(request_)}, callback{callback_},
      magic{magic_}, version{version_} { }
   ~polltask() { if (nullptr != request) snmp_free_pdu(request); }

   polltask(const polltask &other) = delete;
   polltask & operator =(const polltask &other) = delete;

   polltask(polltask &&oter);
   polltask & operator =(polltask &&other) = delete;

   std::string community;
   netsnmp_pdu *request;
   callback_wf callback;
   polldata *pdata {};

   void *magic;
   long version;
};

using taskdata = std::unordered_map<std::string, polltask>;

class mux_poller
{
   public:
      mux_poller(unsigned max_hosts_ = 512) : max_hosts{max_hosts_} { }

      void add(const char *host, const char *community, netsnmp_pdu *request,
            callback_wf callback, void *magic = nullptr, long version = default_version)
      {
         tasks.emplace(std::piecewise_construct, std::forward_as_tuple(host),
               std::forward_as_tuple(community, request, callback, magic, version));
      }

      void clear() { tasks.clear(); }
      void erase(const char *host) { tasks.erase(host); }
      void poll();

   private:
      unsigned max_hosts;
      taskdata tasks;
      std::list<polldata> sessions;
};

} // NAMESPACE END

#endif
