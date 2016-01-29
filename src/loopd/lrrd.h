#ifndef LOOPD_RRD_H
#define LOOPD_RRD_H

#include <string>

class rrd
{
   public:
      rrd(const char *rrdpath_ = nullptr) {
         if (nullptr != rrdpath_) init(rrdpath_); }

      void init(const char *rrdpath);
      void remove();      

   private:
      void create();

      bool valid {false};
      std::string rrdpath;

      static const char *create_params[];
//      static const char *update_params[];
//      static const char *graph_params[];
};

#endif
