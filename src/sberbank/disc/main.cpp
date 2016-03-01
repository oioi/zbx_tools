#include <boost/regex.hpp>
#include <string>
#include <vector>

#include <cstdio>
#include <fcntl.h>

#include "snmp/snmp.h"
#include "snmp/oids.h"

#include "aux_log.h"
#include "prog_config.h"
#include "basic_mysql.h"

#include "data.h"

namespace {
   const char *progname {"sba-disc"};
   const char *conffile {"/etc/zabbix/sberbank/sba-disc.conf"};
   const char *zbx_conffile {"/etc/zabbix/sberbank/zabbix-api.conf"};

   conf::config_map db_section {
      { "host",     { conf::val_type::string } },
      { "username", { conf::val_type::string } },
      { "password", { conf::val_type::string } },
      { "port",     { conf::val_type::integer, 3306 } },
      { "table",    { conf::val_type::string } }
   };

   conf::config_map zabbix_extsection {
      { "screendir",     { conf::val_type::string } },
      { "scrurl-prefix", { conf::val_type::string } },
      { "screen-name",   { conf::val_type::string } }
   };
}

conf::config_map zabbix {
   { "api-url",   { conf::val_type::string } },
   { "username",  { conf::val_type::string } },
   { "password",  { conf::val_type::string } }
};

conf::config_map config {
   { "database",   { conf::val_type::section, &db_section        } },
   { "zabbix-ext", { conf::val_type::section, &zabbix_extsection } },

   { "lockfile",  { conf::val_type::string  } },
   { "sleeptime", { conf::val_type::integer } }
};

void build_screen(const points &source, const std::string &hostname);

points get_sber_vlans(void *sessp)
{
   static const char *funcname {"get_sber_vlans"};
   const oid *oidst = snmp::oids::ifname;
   size_t oidsize = snmp::oids::ifname_size - 1;

   boost::regex vlname {"^br_vlan[34]\\d{3}$"};
   boost::sregex_iterator rgx_end;   
   points hotspots;

   snmp::pdu_handle response;
   std::string ifname;
   char vlan[5];

   for (netsnmp_variable_list *vars;;)
   {
      response = snmp::synch_request(sessp, oidst, oidsize, SNMP_MSG_GETBULK, 0, 30);
      for (vars = response.pdu->variables; nullptr != vars; vars = vars->next_variable)
      {
         if (netsnmp_oid_is_subtree(snmp::oids::ifname, snmp::oids::ifname_size - 1, vars->name, vars->name_length))
            return hotspots;

         if (ASN_OCTET_STR != vars->type)
            throw logging::error {funcname, "Unexpected ASN type in response to ifName"};

         ifname.clear();
         ifname.append(reinterpret_cast<char *>(vars->val.string), vars->val_len);

         boost::sregex_iterator match(ifname.begin(), ifname.end(), vlname);
         if (rgx_end != match)
         {
            ifname.copy(vlan, 4, 7);
            vlan[4] = '\0';
            hotspots.emplace_back(*(vars->name + vars->name_length - 1), vlan);
         }

         if (nullptr == vars->next_variable)
         {
            oidst = vars->name;
            oidsize = vars->name_length;
         }
      }
   }
}

points discover(const std::string &hostname, const std::string &community)
{
   snmp::sess_handle sessp {snmp::init_snmp_session(hostname.c_str(), community.c_str())};
   points hotspots {get_sber_vlans(sessp)};

   const conf::string_t &table {config["database"]["table"].get<conf::string_t>()};
   basic_mysql db(config["database"]["host"].get<conf::string_t>(),
                  config["database"]["username"].get<conf::string_t>(),
                  config["database"]["password"].get<conf::string_t>(),
                  config["database"]["port"].get<conf::integer_t>());

   for (auto &hspot : hotspots)
   {
      if (0 == db.query(true, "select address from %s where ext_vlan = %s or int_vlan = %s",
               table.c_str(), hspot.vlan_name.c_str(), hspot.vlan_name.c_str())) continue;
      hspot.phys_addr = db.get(0, 0);
   }

   buffer json;
   json.print("{\"data\":[");

   for (auto &hotspot : hotspots)
   {
      if (0 == hotspot.phys_addr.size()) continue;
      json.append("{\"{#IFINDEX}\":\"%u\","
                   "\"{#VLAN}\":\"%s\","
                   "\"{#NAME}\":\"%s\"},",
                  hotspot.id, hotspot.vlan_name.c_str(), hotspot.phys_addr.c_str());
   }

   json.pop_back();
   json.append("]}");

   printf(json.data());
   return hotspots;
}

void run(const std::string &hostname, const std::string &community)
{
   init_snmp(progname);
   netsnmp_ds_set_int(NETSNMP_DS_LIBRARY_ID, NETSNMP_DS_LIB_OID_OUTPUT_FORMAT, NETSNMP_OID_OUTPUT_NUMERIC);

   // Performing actual SNMP discovering to obtain available vlans matching our criteria.
   // Also printing data to STDOUT for Zabbix LLD.
   points hotspots {discover(hostname, community)};

   // Zabbix will start this program two times to discover both internal and external VLANs.
   // After actual discovering we want to rebuild screen - last proccess will do that.
   // So we're trying to create lockfile, which should be atomic operation in Linux. 
   // If file was successfuly created then we're first and we don't need to do anything.
   const conf::string_t &lockfile {config["lockfile"].get<conf::string_t>()};
   int fd = open(lockfile.c_str(), O_RDWR | O_CREAT | O_EXCL, 0600);
   if (0 < fd) return;

   if (EEXIST != errno) logger.error_exit(progname, "Failed to create lockfile '%s': %s",
         lockfile.c_str(), strerror(errno));

   // closing descriptors, so zabbix will think that we're done.
   fflush(nullptr);
   close(STDOUT_FILENO);
   close(STDERR_FILENO);   

   pid_t fpid = fork();
   if (0 > fpid) logger.error_exit(progname, "Process fork failed: %s", strerror(errno));
   if (0 < fpid) return;

   // Waiting for discover data to be proccessed by Zabbix
   // then rebuilding screen.
   sleep(config["sleeptime"].get<conf::integer_t>());
   remove(lockfile.c_str());
   build_screen(hotspots, hostname);
}

int main(int argc, char *argv[])
{
   openlog(progname, LOG_PID, LOG_LOCAL7);
   if (4 != argc) logger.error_exit(progname, "%s: wrong arg count: [ip] [snmp_community] [placeholder]", argv[0]);

   try {
      if (0 == conf::read_config(conffile, config))
         logger.error_exit(progname, "Errors while reading configuration file.");

      if (0 == conf::read_config(zbx_conffile, zabbix))
         logger.error_exit(progname, "Error while reading zabbix configuration file.");

      std::string hostname {argv[1]};
      std::string community {argv[2]};
      run(hostname, community);
   }

   catch (logging::error &error) {
      logger.error_exit(progname, error.what());
   }

   catch (std::exception &exc) {
      logger.error_exit(progname, exc.what());
   }

   catch (...) {
      logger.error_exit(progname, "Aborted by generic catch. Something went really bad.");
   }

   return 0;
}
