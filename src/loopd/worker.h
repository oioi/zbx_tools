#ifndef LOOPD_WORKER_H
#define LOOPD_WORKER_H

#include <mutex>
#include <thread>
#include <condition_variable>

#include "prog_config.h"

struct thread_sync
{
   std::mutex device_datalock;
   std::mutex worker_datalock;

   std::mutex statelock;
   bool sleeping {true};
   bool running {true};
   std::condition_variable wake;

   std::mutex updatelock;
   bool data_updated {false};
};

void worker(thread_sync *);

#endif
