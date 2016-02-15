#include "snmp/snmp.h"

namespace {
   const char *progname {"llid"};
}

void escape_string(std::string &source)
{
   std::size_t pos = std::string::npos;
   while (std::string::npos != (pos = source.find('"', (pos == std::string::npos) ? 0 : pos + 2)))
      source.replace(pos, sizeof(char), "\\\"");
}

buffer get_hostdata(const char *host, const char *community)
{
   snmp::sess_handle sessp {snmp::init_snmp_session(host, community)};
   snmp::intdata ints {snmp::get_host_physints(sessp)};
   snmp::intinfo info {snmp::get_intinfo(sessp, ints)};

   buffer json;
   json.print("{\"data\":[");

   for (auto &entry : info)
   {
      if (false == entry.active) continue;
      escape_string(entry.name);
      escape_string(entry.alias);

      json.append("{\"{#IFINDEX}\":\"%u\","
                   "\"{#IFNAME}\":\"%s\","
                   "\"{#IFALIAS}\":\"%s\","
                   "\"{#IFSPEED}\": \"%u\"},",
            entry.id, entry.name.c_str(), entry.alias.c_str(), entry.speed);
   }

   json.pop_back();
   json.append("]}");
   return json;
}


int main(int argc, char *argv[])
{
   openlog(progname, LOG_PID, LOG_LOCAL7);
   if (3 != argc) logger.error_exit(progname, "%s: should be called with exactly two arguments: [ip] [snmp_community]", argv[0]);
   if (0 == strcmp("127.0.0.1", argv[1])) return 0;

   buffer json;
   try { json = get_hostdata(argv[1], argv[2]); }

   catch (snmp::snmprun_error &error) {
      logger.error_exit(progname, "device discovery aborted by SNMP error: %s", error.what());
   }

   catch (logging::error &error) {
      logger.error_exit(progname, "device discovery aborted by generic error: %s", error.what());
   }

   catch (...) {
      logger.error_exit(progname, "device discovery aborted by generic catch.");
   }

   printf(json.data());
   return 0;
}
