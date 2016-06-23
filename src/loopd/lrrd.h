#ifndef LOOPD_RRD_H
#define LOOPD_RRD_H

#include <string>

class rrd
{
   public:
      void init(const char *rrdpath, unsigned step);
      void remove();

      void graph(const char *filename, const char *title, int xsize = 500, int ysize = 120);
      void add_data(double val, double mav);

   private:
      void create();

      bool valid {false};
      std::string rrdpath;
      unsigned step;

      static const char *create_params[];
      static const char *update_params[];
      static const char *graph_params[];
};

#endif
