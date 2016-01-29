#include <sys/stat.h>
#include <sys/types.h>
#include <rrd.h>

#include "lrrd.h"
#include "aux_log.h"
#include "prog_config.h"

// This is not a generic interface to RRD of any kind. Used only for internal purposes.
// All further code is like super ugly.
// And RRD API is kinda fucked up. >_<

const char * rrd::create_params[] = {
   "rrdcreate",
   "--step",
   nullptr, // step in seconds
   nullptr,
   nullptr,
   nullptr,
   nullptr,
   nullptr
};

void rrd::init(const char *rrdpath_)
{
   static const char *funcname {"rrd::init"};
   struct stat stbuf;

   rrdpath = rrdpath_;
   if (-1 == stat(rrdpath.c_str(), &stbuf))
   {
      if (ENOENT == errno) create();
      else throw logging::error {funcname, "cannot stat path '%s': %s", rrdpath.c_str(), strerror(errno)};
   }

   valid = true;
   // NOTE: we probably should check that existing RRD actually meet our poller settings.
   // If not, then only way is to recreate RRD and flush all interface data.
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

void rrd::create()
{
   static const char *funcname {"rrd::create"};
   static const conf::integer_t seconds {config["poller"]["poll_interval"].get<conf::integer_t>()};

   buffer step, ds1, ds2, rra;
   step.print("%d", seconds);
   ds1.print("DS:broadcast:GAUGE:%lu:0:U", (unsigned long) (seconds * 1.2));
   ds2.print("DS:maverage:GAUGE:%lu:0:U", (unsigned long) (seconds * 1.2));
   rra.print("RRA:LAST:0:1:%u", (86400 / seconds) + 10); // 24 hours of data + 10 to be sure

   create_params[2] = step.data();
   create_params[3] = rrdpath.c_str();
   create_params[4] = ds1.data();
   create_params[5] = ds2.data();
   create_params[6] = rra.data();   

   optind = opterr = 0;
   int result = rrd_create(7, const_cast<char **>(create_params));
   if (rrd_test_error() or 0 != result) 
      throw logging::error {funcname, "RRD Create error: %s", rrd_get_error()};
}
