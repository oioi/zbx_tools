#ifndef BASIC_MYSQL_H
#define BASIC_MYSQL_H

#include <vector>

#include <mysql/mysql.h>
#include "buffer.h"

class basic_mysql
{
   public:
      basic_mysql(const char *host, const char *username, const char *password, int port);
      ~basic_mysql();

      unsigned int query(bool store, const char *format, ...) __attribute__((format(printf,3,4)));
      const char *get(unsigned int rownum, unsigned int colnum);

   private:
      MYSQL mysql;
      MYSQL *conn;
      MYSQL_RES *result;

      buffer query_str;
      unsigned int rows_count;
      std::vector<MYSQL_ROW> rows;
};


#endif
