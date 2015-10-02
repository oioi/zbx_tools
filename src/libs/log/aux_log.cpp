#include <stdio.h>
#include <stdlib.h>
#include "aux_log.h"

namespace logging {

static const char *prioritynames[] = {
   "EMERG",
   "ALERT",
   "CRIT",
   "ERR",
   "WARNING",
   "NOTICE",
   "INFO",
   "DEBUG",
   nullptr
};

void default_errstr(buffer &message, int priority, const char *funcname, const char *format, va_list args)
{
   if (0 <= priority) message.print("%s: ", prioritynames[priority]);
   message.append("%s(): ", funcname);
   message.vappend(format, args);
}

error::error(const char *funcname, const char *format, ...) noexcept
{
   va_list args;
   va_start(args, format);
   default_errstr(message, -1, funcname, format, args);
   va_end(args);
}

void basic_logger::write_message(int priority, const char *funcname, const char *format, va_list args)
{
   default_errstr(msg_buffer, priority, funcname, format, args);
   if (static_cast<bool>(method & log_method::M_SYSLOG))  syslog(priority, msg_buffer.data());
   if (static_cast<bool>(method & log_method::M_FILE))    fprintf(logfile, "%s\n", msg_buffer.data());
   if (static_cast<bool>(method & log_method::M_STDE))    fprintf(stderr, "%s\n", msg_buffer.data());
   if (static_cast<bool>(method & log_method::M_STDO))    printf("%s\n", msg_buffer.data());
}

void basic_logger::log_message(int priority, const char *funcname, const char *format, ...)
{
   va_list args;
   va_start(args, format);
   write_message(priority, funcname, format, args);
   va_end(args);
}

void basic_logger::error_exit(const char *funcname, const char *format, ...)
{
   va_list args;
   va_start(args, format);
   write_message(LOG_CRIT, funcname, format, args);
   va_end(args);
   exit(LOG_CRIT);
}

} // LOGGING NAMESPACE

logging::basic_logger logger;
