#include <fstream>

#include "aux_log.h"
#include "prog_config.h"
#include "zbx_api.h"

#include "data.h"

void generate_screen_names(ordered_points &hotspots)
{
   const conf::string_t &datadir {config["zabbix"]["screendir"].get<conf::string_t>() + '/'};
   const conf::string_t &url_prefix {config["zabbix"]["scrurl-prefix"].get<conf::string_t>() + '/'};

   std::hash<std::string> hash_fn;
   buffer filename;

   for (auto &hspot : hotspots)
   {
      size_t addrhash = hash_fn(hspot.first);
      filename.print("%lu.html", addrhash);

      hspot.second.nameurl = url_prefix + filename.data();
      std::ofstream file {datadir + filename.data(), std::ofstream::trunc};

      file << "<html><head><meta charset=\"utf-8\">"
              "<link rel=\"stylesheet\" type=\"text/css\" href=\"styles.css\">"
              "</head><body><div>"
           << hspot.first << "</div></body></html>";
   }
}

void get_graph_byitem(zbx_api::api_session &zbx_sess, unsigned hostid, const char *itemkey, unsigned long *graphid)
{
   static const char *funcname {"get_graphid_byitem"};
   uint_t itemid;

   zbx_sess.send_vstr(R"**(
      "method": "item.get",
      "params": {
         "output": "itemid",
            "hostids": "%u",
            "filter": { "key_": "%s" } }
    )**", hostid, itemkey);

    if (false == zbx_sess.json_get_uint("result[0].itemid", &itemid))
       throw logging::error {funcname, "Failed to obtain item with key '%s'", itemkey};

    zbx_sess.send_vstr(R"**(
       "method": "graph.get",
          "params": {
             "output": "graphid",
             "itemids": "%lu" }
    )**", itemid);

   if (false == zbx_sess.json_get_uint("result[0].graphid", graphid))
      throw logging::error {funcname, "Failed to obtain graphid used for item with key '%s'", itemkey};
}

void get_graph_ids(zbx_api::api_session &zbx_sess, ordered_points &hotspots, unsigned hostid)
{
   buffer itemkey;

   for (auto &hspot : hotspots)
   {
      if (0 != hspot.second.int_vlan.size())
      {
         itemkey.print("INT_ifHCInOctets[%u]", hspot.second.int_id);
         get_graph_byitem(zbx_sess, hostid, itemkey.data(), &(hspot.second.int_traffic_graphid));

         itemkey.print("users[%s]", hspot.second.int_vlan.c_str());
         get_graph_byitem(zbx_sess, hostid, itemkey.data(), &(hspot.second.int_users_graphid));
      }

      if (0 != hspot.second.ext_vlan.size())
      {
         itemkey.print("EXT_ifHCInOctets[%u]", hspot.second.ext_id);
         get_graph_byitem(zbx_sess, hostid, itemkey.data(), &(hspot.second.ext_traffic_graphid));

         itemkey.print("authorized[%s]", hspot.second.ext_vlan.c_str());
         get_graph_byitem(zbx_sess, hostid, itemkey.data(), &(hspot.second.ext_users_graphid));
      }
   }
}

buffer generate_screen_items(const ordered_points &hotspots, unsigned *vsize)
{
   unsigned y {};
   buffer items;

   for (const auto &hspot : hotspots)
   {
      const addr_point_data &data = hspot.second;

      items.append("{\"resourcetype\": \"11\", \"colspan\": \"2\", \"height\": \"45\", "
                    "\"url\": \"%s\", \"width\": \"800\", \"x\": \"0\", \"y\": \"%u\"},",
                     data.nameurl.c_str(), y++);
      if (0 == data.int_traffic_graphid or 0 == data.ext_traffic_graphid)
      {
         items.append("{\"resourcetype\": \"0\", \"colspan\": \"1\", \"height\": \"100\","
                       "\"resourceid\": \"%lu\" , \"width\": \"500\", \"x\": \"0\", \"y\": \"%u\"},",
                       0 != data.int_traffic_graphid ? data.int_traffic_graphid : data.ext_traffic_graphid, y++);

         items.append("{\"resourcetype\": \"0\", \"colspan\": \"1\", \"height\": \"100\","
                       "\"resourceid\": \"%lu\" , \"width\": \"500\", \"x\": \"0\", \"y\": \"%u\"},",
                       0 != data.int_users_graphid ? data.int_users_graphid : data.ext_users_graphid, y++);

         continue;
      }

      items.append("{\"resourcetype\": \"0\", \"colspan\": \"1\", \"height\": \"100\","
                    "\"resourceid\": \"%lu\" , \"width\": \"500\", \"x\": \"1\", \"y\": \"%u\"},",
                    data.int_traffic_graphid, y);

      items.append("{\"resourcetype\": \"0\", \"colspan\": \"1\", \"height\": \"100\","
                    "\"resourceid\": \"%lu\" , \"width\": \"500\", \"x\": \"0\", \"y\": \"%u\"},",
                    data.ext_traffic_graphid, y++);

      items.append("{\"resourcetype\": \"0\", \"colspan\": \"1\", \"height\": \"100\","
                    "\"resourceid\": \"%lu\" , \"width\": \"500\", \"x\": \"1\", \"y\": \"%u\"},",
                    data.int_users_graphid, y);

      items.append("{\"resourcetype\": \"0\", \"colspan\": \"1\", \"height\": \"100\","
                    "\"resourceid\": \"%lu\" , \"width\": \"500\", \"x\": \"0\", \"y\": \"%u\"},",
                    data.ext_users_graphid, y++);      
   }

   *vsize = y;
   items.pop_back();
   return items;
}

void rebuild_screen(ordered_points &hotspots, const std::string &hostname)
{
   static const char *funcname {"rebuild_screen"};

   zbx_api::api_session zbx_sess;
   zbx_sess.set_auth(config["zabbix"]["api-url"].get<conf::string_t>(),
                     config["zabbix"]["username"].get<conf::string_t>(),
                     config["zabbix"]["password"].get<conf::string_t>());

   zbx_sess.send_vstr(R"**(
      "method": "host.get",
      "params": {
         "output": "hostid",
         "filter": { "ip": "%s" } }
   )**", hostname.c_str());

   uint_t hostid;
   if (false == zbx_sess.json_get_uint("result[0].hostid", &hostid))
      throw logging::error {funcname, "Failed to obtaing hostid for host: %s", hostname.c_str()};

   const char *screen_name {config["zabbix"]["screen-name"].get<conf::string_t>().c_str()};

   zbx_sess.send_vstr(R"**(
      "method": "screen.get",
      "params": {
         "output": "screenid",
         "filter": { "name": "%s" } }
   )**", screen_name);

   uint_t screen_id;
   if (false == zbx_sess.json_get_uint("result[0].screenid", &screen_id))
      throw logging::error {funcname, "Failed to obtain screenid for screen with name '%s'", screen_name};

   unsigned vsize;
   get_graph_ids(zbx_sess, hotspots, hostid);
   buffer items {generate_screen_items(hotspots, &vsize)};

   zbx_sess.send_vstr(R"**(
      "method": "screen.update",
      "params": {
         "screenid": "%lu",
         "vsize": "%u",
         "screenitems": [ %s ] }
   )**", screen_id, vsize, items.data());
}

void build_screen(const points &source, const std::string &hostname)
{
   ordered_points hotspots;

   for (auto &hspot : source)
   {
      if (0 == hspot.phys_addr.size()) continue;
      addr_point_data &data = hotspots[hspot.phys_addr];

      if  ('3' == hspot.vlan_name[0])
      {
         data.int_vlan = hspot.vlan_name;
         data.int_id = hspot.id;
      }

      else
      {
         data.ext_vlan = hspot.vlan_name;
         data.ext_id = hspot.id;
      }
   }

   generate_screen_names(hotspots);
   rebuild_screen(hotspots, hostname);
}
