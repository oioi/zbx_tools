#include "snmp/mux_poller.h"

namespace snmp {

polltask::polltask(polltask &&other)
{
   std::swap(host, other.host);
   std::swap(community, other.community);

   request = other.request;
   callback = other.callback;
   pdata = other.pdata;
   magic = other.magic;
   version = other.version;
   other.request = nullptr;
}

extern "C" int callback_wrap(int operation, snmp_session *sessp, int reqid, netsnmp_pdu *pdu, void *magic)
{
   polltask *task = static_cast<polltask *>(magic);
   if (ok_close == task->callback(operation, sessp, reqid, pdu, task->magic, task->pdata->sessp))
      task->pdata->state = pollstate::finished;
   return 1;
}

void mux_poller::poll()
{
   static const char *funcname {"snmp::mux_poller::poll"};

   size_t tasksize = tasks.size();
   if (0 == tasksize) return;

   int fds, block;
   fd_set fdset;
   timeval timeout;

   for (unsigned active_hosts, id = 0;;)
   {
      active_hosts = sessions.size();
      if (active_hosts < max_hosts)
      {
         if (0 == active_hosts and id == tasksize) return;
         unsigned delta = max_hosts - active_hosts;
         void *sessp;

         for (unsigned i = 0; id < tasksize and i < delta; i++, id++)
         {
            polltask &task = tasks[id];
            sessp = init_snmp_session(task.host.c_str(), task.community.c_str(),
                  task.version, callback_wrap, static_cast<void *>(&task));

            sessions.emplace_front(sessp);
            task.pdata = &(sessions.front());

            try { async_send(sessp, snmp_clone_pdu(task.request)); }
            catch (snmprun_error &error) { 
               throw snmprun_error {funcname, "poll failed: %s", error.what()}; }
         }
      }

      fds = block = 0;
      FD_ZERO(&fdset);
      
      for (auto &sess : sessions) snmp_sess_select_info(sess.sessp, &fds, &fdset, &timeout, &block);
      if (0 > (fds = select(fds, &fdset, nullptr, nullptr, &timeout)))
         throw snmprun_error {funcname, "select() failed: %s", strerror(errno)};

      if (fds) { for (auto &sess : sessions) snmp_sess_read(sess.sessp, &fdset); }
      else     { for (auto &sess : sessions) snmp_sess_timeout(sess.sessp);      }

      sessions.remove_if([](const polldata &p) { return (pollstate::finished == p.state); });
   }
}

}
