#include <vector>
#include <cmath>

#include "buffer.h"
#include "snmp/snmp.h"

namespace {
   const char *progname {"cisco-tempdisc"};
}

struct sensor_info_st
{
   unsigned id;
   std::string name;
   double scale;

   sensor_info_st(unsigned id_) : id {id_} { }
};

using sensors_data = std::vector<sensor_info_st>;

void escape_string(std::string &source)
{
   std::size_t pos = std::string::npos;
   while (std::string::npos != (pos = source.find('"', (pos == std::string::npos) ? 0 : pos + 2)))
      source.replace(pos, sizeof(char), "\\\"");
}

sensor_info_st parse_info(netsnmp_variable_list *vars, unsigned id)
{
   static const char *funcname {"parse_info"};
   sensor_info_st info {id};
   int i = 0;

   for (; nullptr != vars; vars = vars->next_variable, ++i)
   {
      switch (i)
      {
         case 0:
            if (ASN_OCTET_STR != vars->type)
               throw snmp::snmprun_error {snmp::errtype::invalid_data, funcname, "Unexpected ASN type in answer for name"};
            info.name.append(reinterpret_cast<char *>(vars->val.string), vars->val_len);
            break;

         case 1:
            if (ASN_INTEGER != vars->type)
               throw snmp::snmprun_error {snmp::errtype::invalid_data, funcname, "Unexpected ASN type in answer for scale"};
            info.scale = pow(10.0, (*(vars->val.integer) - 9) * 3);
            break;

         case 2:
            if (ASN_INTEGER != vars->type)
               throw snmp::snmprun_error {snmp::errtype::invalid_data, funcname, "Unexpected ASN type in answer for precision"};            
            info.scale *= pow(10.0, -(*(vars->val.integer)));
            break;
      }
   }

   if (3 != i) throw snmp::snmprun_error {snmp::errtype::invalid_data, funcname, "Wrong count of data oids while parsing."};
   return info;
}

sensors_data get_sensors_info(void *sessp, const std::vector<unsigned> &sensors)
{
   oid sensors_name[] = { 1, 3, 6, 1, 2, 1, 47, 1, 1, 1, 1, 2, 0 };
   const size_t namesize = sizeof(sensors_name) / sizeof(oid);

   oid sensor_scale[] = { 1, 3, 6, 1, 4, 1, 9, 9, 91, 1, 1, 1, 1, 2, 0 };
   const size_t scalesize = sizeof(sensor_scale) / sizeof(oid);

   oid sensor_precision[] = { 1, 3, 6, 1, 4, 1, 9, 9, 91, 1, 1, 1, 1, 3, 0 };
   const size_t precisionsize = sizeof(sensor_precision) / sizeof(oid);

   snmp::pdu_handle response;
   netsnmp_pdu *request;

   std::string name;
   sensors_data sensors_info;

   for (unsigned id : sensors)
   {
      sensors_name[namesize - 1] = id;
      sensor_scale[scalesize - 1] = id;
      sensor_precision[precisionsize - 1] = id;

      request = snmp_pdu_create(SNMP_MSG_GET);
      snmp_add_null_var(request, sensors_name, namesize);
      snmp_add_null_var(request, sensor_scale, scalesize);
      snmp_add_null_var(request, sensor_precision, precisionsize);

      response = snmp::synch_request(sessp, request);
      sensors_info.push_back(parse_info(response.pdu->variables, id));
   }

   return sensors_info;
}

buffer get_host_tempsensors(const char *host, const char *community)
{
   const oid sensors[] { 1, 3, 6, 1, 4, 1, 9, 9, 91, 1, 1, 1, 1, 1 };
   const size_t sensors_size = sizeof(sensors) / sizeof(oid);

   snmp::sess_handle sessp {snmp::init_snmp_session(host, community)};   

   // 8 - temperature sensor type
   std::vector<unsigned> temp_sensors {get_nodes_bytype(sessp, sensors, sensors_size, { 8 })}; 
   sensors_data sensors_info {get_sensors_info(sessp, temp_sensors)};

   buffer json;
   json.print("{\"data\":[");

   for (auto &entry : sensors_info)
   {
      escape_string(entry.name);
      json.append("{\"{#INDEX}\":\"%u\","
                   "\"{#SCALE}\":\"%f\","
                   "\"{#NAME}\":\"%s\"},",
                   entry.id, entry.scale, entry.name.c_str());
   }

   json.pop_back();
   json.append("]}");
   return json;
}

int main(int argc, char *argv[])
{
   openlog(progname, LOG_PID, LOG_LOCAL7);
   if (3 != argc) logger.error_exit(progname, "%s: should be called with exactly two arguments: [ip] [community]", progname);

   buffer json;
   try { json = get_host_tempsensors(argv[1], argv[2]); }

   catch (std::exception &exc) {
      logger.error_exit(progname, "%s", exc.what());
   }

   catch (...) {
      logger.error_exit(progname, "aborted by generic exception.");
   }

   printf(json.data());
   return 0;
}
