#ifndef BASIC_MYSQL_H
#define BASIC_MYSQL_H

#include <string>
#include <vector>

#include <mysql/mysql.h>
#include "buffer.h"

class basic_mysql
{
   public:
      basic_mysql(const char *host, const char *username, const char *password, int port) : 
         result(nullptr), rows_count(0) { init(host, username, password, port); }
      basic_mysql(const std::string &host, const std::string &username, const std::string password, int port) :
         result(nullptr), rows_count(0) { init(host.c_str(), username.c_str(), password.c_str(), port); }
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

      void init(const char *host, const char *username, const char *password, int port);
};

#endif
