#ifndef AUX_LOGGING_H
#define AUX_LOGGING_H

#include <stdexcept>
#include <syslog.h>
#include <errno.h>
#include <string.h>

#include "buffer.h"

namespace logging {

enum class log_method : int {
   M_SYSLOG = 1 << 0,
   M_FILE   = 1 << 1,
   M_STDE   = 1 << 2,
   M_STDO   = 1 << 3
};

struct error : public std::exception
{
   buffer message;

   char const *what() const noexcept { return message.data(); }
   error(const char *funcname, const char *format, ...) noexcept
      __attribute__((format(printf,3,4)));
   ~error() noexcept { }
};

inline constexpr log_method operator &(log_method A, log_method B) {
   return static_cast<log_method>(static_cast<int>(A) & static_cast<int>(B)); }

inline constexpr log_method operator |(log_method A, log_method B) {
   return static_cast<log_method>(static_cast<int>(A) | static_cast<int>(B)); }

class basic_logger
{
   public:
      int default_priority;
      log_method method;

      basic_logger() : default_priority(LOG_INFO), method(log_method::M_SYSLOG) { }

      void log_vmessage(int priority, const char *funcname, const char *format, va_list args) {
         write_message(priority, funcname, format, args); }
      void log_message(int priority, const char *funcname, const char *format, ...)
         __attribute__((format(printf,4,5)));


      __attribute__((noreturn)) void error_exit(const char *funcname, const char *format, ...)
         __attribute__((format(printf,3,4)));      

   private:
      buffer msg_buffer;
      FILE *logfile;

      void write_message(int priority, const char *funcname, const char *format, va_list args);
};

} // LOGGING NAMESPACE

extern logging::basic_logger logger;

#endif
