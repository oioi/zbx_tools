#include <sys/stat.h>
#include <sys/types.h>
#include <rrd.h>

#include "aux_log.h"
#include "lrrd.h"

#include <iostream>

const char * rrd::create_params[] = {
   "rrdcreate",
   "--step",
   nullptr,
   nullptr,
   nullptr,
   nullptr,
   nullptr,
   nullptr
};

const char * rrd::update_params[] = {
   "rrdupdate",
   nullptr,
   nullptr,
   nullptr
};

const char * rrd::graph_params[] = {
   "rrdgraph",
   nullptr,     // Graph path   
   "--end",
   nullptr,
   "--height",
   nullptr,
   "--width",
   nullptr,
   "--title",
   nullptr,     // Title
   "--vertical-label=broadcast pps",
   nullptr,     // DEF:bc [10]
   nullptr,     // DEF:mv [11]
   "LINE1:bc#B8B8B8",
   "LINE1:mv#000000",
   "GPRINT:bc:LAST:Last bps value\\: \%8.2lf \%s",
   "GPRINT:mv:LAST:Moving average (1Hr)\\: \%8.2lf \%s",
   nullptr
};

void rrd::init(const char *rrdpath_, unsigned step_)
{
   static const char *funcname {"rrd::init"};
   struct stat stbuf;

   step = step_;
   rrdpath = rrdpath_;

   if (-1 == stat(rrdpath.c_str(), &stbuf))
   {
      if (ENOENT == errno) create();
      else throw logging::error {funcname, "cannot stat path '%s': %s",
         rrdpath.c_str(), strerror(errno)};
   }

   valid = true;
   // NOTE: we should probably check that existing RRD actually meet our poller settings.
   // If not, then only way is to recreate RRD and flush all interface data.
}

void rrd::create()
{
   static const char *funcname {"rrd::create"};

   buffer seconds, ds1, ds2, rra;
   seconds.print("%d", step);
   ds1.print("DS:broadcast:GAUGE:%lu:0:U", (unsigned long) (step * 1.2));
   ds2.print("DS:maverage:GAUGE:%lu:0:U", (unsigned long) (step * 1.2));
   rra.print("RRA:LAST:0:1:%u", (86400 / step) + 10); // 24 hours of data + 10 to be sure

   create_params[2] = seconds.data();
   create_params[3] = rrdpath.c_str();
   create_params[4] = ds1.data();
   create_params[5] = ds2.data();
   create_params[6] = rra.data();   

   optind = opterr = 0;
   int result = rrd_create(7, const_cast<char **>(create_params));
   if (rrd_test_error() or 0 != result) 
      throw logging::error {funcname, "RRD Create error: %s", rrd_get_error()};
}

void rrd::add_data(double val, double mav)
{
   static const char *funcname {"rrd::add_data"};
   if (!valid) return;

   buffer datastr;
   datastr.print("N:%f:%f", val, mav);
   update_params[1] = rrdpath.c_str();
   update_params[2] = datastr.data();

   optind = opterr = 0;
   int result = rrd_update(3, const_cast<char **>(update_params));
   if (rrd_test_error() or 0 != result)
      throw logging::error {funcname, "RRD Update error: %s", rrd_get_error()};
}

void rrd::remove()
{
   static const char *funcname {"rrd::remove"};
   if (!valid) return;

   if (-1 == ::remove(rrdpath.c_str()))
      throw logging::error {funcname, "failed to remove RRD file '%s': %s",
         rrdpath.c_str(), strerror(errno)};
   valid = false;
}

void rrd::graph(const char *filename, const char *title, int xsize, int ysize)
{
   static const char *funcname {"rrd::graph"};
   buffer end, def1, def2, height, width;

   graph_params[1] = filename;

   end.print("%lu", time(nullptr) - step);
   graph_params[3] = end.data();

   height.print("%d", ysize);
   width.print("%d", xsize);
   graph_params[5] = height.data();
   graph_params[7] = width.data();
   graph_params[9] = title;

   def1.print("DEF:bc=%s:broadcast:LAST", rrdpath.c_str());
   def2.print("DEF:mv=%s:maverage:LAST", rrdpath.c_str());
   graph_params[11] = def1.data();
   graph_params[12] = def2.data();

   char **calcpr {};
   int result;
   double ymin, ymax;

   for (const char **ptr = graph_params; nullptr != *ptr; ++ptr) std::cerr << *ptr << std::endl;
   std::cerr << std::endl;

   optind = opterr = 0;
   result = rrd_graph(17, const_cast<char **>(graph_params), &calcpr, &xsize, &ysize, nullptr, &ymin, &ymax);

   if (calcpr) { for (unsigned int i = 0; (calcpr[i]); i++) free(calcpr[i]); }
   if (rrd_test_error() or 0 != result)
      throw logging::error {funcname, "RRD Graph failed: %s", rrd_get_error()};
}
