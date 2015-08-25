#include "basic_mysql.h"
#include "aux_log.h"

basic_mysql::~basic_mysql()
{
   if (nullptr != result) mysql_free_result(result);
   mysql_close(&mysql);
}

void basic_mysql::init(const char *host, const char *username, const char *password, int port)
{
   mysql_init(&mysql);
   if (nullptr == (conn = mysql_real_connect(&mysql, host, username, password, nullptr, port, nullptr, 0)))
      throw logging::error("basic_mysql", "Cannot connect to DB '%s' : %s", host, mysql_error(&mysql));
   mysql_set_character_set(&mysql, "utf8");
}

unsigned int basic_mysql::query(bool store, const char *format, ...)
{
   static const char *funcname = "basic_mysql::query";

   va_list args;
   va_start(args, format);
   query_str.vprint(format, args);
   va_end(args);

   if (0 != mysql_query(conn, query_str.data()))
      throw logging::error(funcname, "Error while after query '%s' : %s", query_str.data(), mysql_error(&mysql));

   if (!store) return 0;
   if (nullptr != result) mysql_free_result(result);
   if (nullptr == (result = mysql_store_result(conn)))
      throw logging::error(funcname, "Error storing result from query '%s' : %s", query_str.data(), mysql_error(&mysql));

   rows.clear();
   for (MYSQL_ROW frow; nullptr != (frow = mysql_fetch_row(result)); ) rows.push_back(frow);
   return rows_count = mysql_num_rows(result);
}

const char * basic_mysql::get(unsigned int rownum, unsigned int colnum)
{
   if (rownum > rows_count) return nullptr;
   return (rows[rownum])[colnum];
}


