#include "aux_log.h"
#include "buffer.h"
#include "sqlite_db.h"

sqlite_db::sqlite_db(const char *filename)
{
   if (SQLITE_OK != sqlite3_open(filename, &db))
   {
      buffer errmsg;
      errmsg.print("Cannot open sqlite db '%s': %s", filename, sqlite3_errmsg(db));
      sqlite3_close(db);
      throw logging::error("sqlite_db::sqlite_db", errmsg.data());
   }
   cbs.push_back(nullptr);
}

int sqlite_db::register_callback(callback ptr)
{
   size_t size = cbs.size();
   cbs.push_back(ptr);
   return size;
}

void sqlite_db::exec(const char *query, void *data, int callback_numb)
{
   char *errmsg {};
   if (SQLITE_OK != sqlite3_exec(db, query, cbs[callback_numb], data, &errmsg))
      throw logging::error("sqlite_db::exec", "SQL query error: %s", errmsg);
}

void sqlite_db::trans_exec(const char *query, void *data, int callback_numb)
{
   exec("begin");
   exec(query, data, callback_numb);
   exec("end");
}
