#ifndef AUX_SQLITE_H
#define AUX_SQLITE_H

#include <vector>
#include <sqlite3.h>

using callback = int (*) (void *, int, char **, char **);

class sqlite_db
{
   public:
      sqlite_db(const char *filename);
      ~sqlite_db() { sqlite3_close(db); }

      void exec(const char *query, void *data, int callback_numb);
      void exec(const char *query, void *data = nullptr) { exec(query, data, default_callback); }

      void trans_exec(const char *query, void *data, int callback_numb);
      void trans_exec(const char *query, void *data = nullptr) { trans_exec(query, data, default_callback); }

      int register_callback(callback ptr);

   private:
      sqlite3 *db;
      std::vector<callback> cbs;
      int default_callback {0};
};

#endif
