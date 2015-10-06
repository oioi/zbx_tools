#include <vector>
#include <sstream>
#include <memory>
#include <cstring>

#include <confuse.h>

#include "aux_log.h"
#include "prog_config.h"

namespace conf {

using section_ptrs = std::vector<std::unique_ptr<cfg_opt_t>>;
const std::vector<std::vector<std::type_index>> allowed_types {
   { std::type_index(typeid(integer_t)) },
   { std::type_index(typeid(string_t)), std::type_index(typeid(char *)) },
   { std::type_index(typeid(multistring_t)) },
   { std::type_index(typeid(section_t *)) }
};

void config_entry::check_type(const std::type_info &requested) const
{
   static const char *funcname = "config_entry::check_type";

   if (type == val_type::unknown) throw logging::error(funcname, "Attempt to access unknown-type config value.");
   for (auto &index : allowed_types[static_cast<int>(type)])
      if (index == std::type_index(requested)) return;
   throw logging::error(funcname, "Request type mismatch with current value. Requested: %s.", requested.name());
}

template <typename T>
void config_entry::set(const T& newval)
{
   static const char *funcname = "config_entry::set_value";

   check_type(typeid(T));
   if (locked) throw logging::error(funcname, "Attempt to set value on already set and locked entry.");

   locked = true;
   value = newval;
}

cfg_opt_t * build_section(section_t *section, section_ptrs &ptrs)
{
   static const char *funcname = "conf::build_section";

   cfg_opt_t *ptr = new cfg_opt_t[section->size() + 1];
   ptrs.push_back(std::unique_ptr<cfg_opt_t>(ptr));

   for (auto &entry : *section)
   {
      char *name = const_cast<char *>(entry.first.c_str());

      switch (entry.second.what_type())
      {
         case val_type::integer:     *ptr = CFG_INT(name, 0, CFGF_NODEFAULT); break;
         case val_type::string:      *ptr = CFG_STR(name, 0, CFGF_NODEFAULT); break;
         case val_type::multistring: *ptr = CFG_STR_LIST(name, 0, CFGF_NODEFAULT); break;
         case val_type::section:     *ptr = CFG_SEC(name, build_section(entry.second.get<conf::section_t *>(), ptrs), CFGF_NONE); break;

         case val_type::unknown:
            throw logging::error(funcname, "Val with unknown type in section: %s", name);
      }
      ptr++;
   }

   *ptr = CFG_END();
   return ptrs.back().get();
}

void read_cfg_section(section_t *section, cfg_t *cfg_sec, std::stringstream &errors)
{
   static const char *funcname = "conf::read_cfg_section";

   for (auto &entry : *section)
   {
      char *name = const_cast<char *>(entry.first.c_str());
      bool required = entry.second.is_required();

      switch (entry.second.what_type())
      {
         case val_type::integer:
         {
            integer_t val;
            if (0 == (val = cfg_getint(cfg_sec, name))) {
               if (required) errors << "Required int-option '" << name << "' is not set." << std::endl; }
            else entry.second.set(val);
            break;
         }

         case val_type::string:
         {
            char *val;
            if (nullptr == (val = cfg_getstr(cfg_sec, name))) {
               if (required) errors << "Required str-option '" << name << "' is not set." << std::endl; }
            else entry.second.set(val);
            break;
         }

         case val_type::multistring:
         {
            if (nullptr == cfg_getstr(cfg_sec, name)) {
               if (required) errors << "Required multistr-option '" << name << "' is not set." << std::endl; }
            else
            {
               int count = cfg_size(cfg_sec, name);
               multistring_t strs;

               for (int i = 0; i < count; i++) 
                  strs.push_back(std::move(string_t(cfg_getnstr(cfg_sec, name, i))));
               entry.second.set(strs);
            }
            break;
         }

         case val_type::section:
            read_cfg_section(entry.second.get<conf::section_t *>(), cfg_getsec(cfg_sec, name), errors);
            break;

         case val_type::unknown:
            throw logging::error(funcname, "Val with unknown type in section: %s", name);   
      }
   }
}

void cfg_error_fnc(cfg_t*, const char *format, va_list ap)
{
   logger.log_vmessage(LOG_ERR, "cfg_parse", format, ap);
}

int read_config(const char *filename, section_t &config)
{
   static const char *funcname = "conf::read_config";

   section_ptrs ptrs;
   build_section(&config, ptrs);

   cfg_t *cfg = cfg_init(ptrs[0].get(), CFGF_NONE);
   cfg_set_error_function(cfg, cfg_error_fnc);

   switch(cfg_parse(cfg, filename))
   {
      case CFG_FILE_ERROR:
         throw logging::error(funcname, "Configuration file '%s' cannot be read: %s", filename, strerror(errno));
      case CFG_PARSE_ERROR:
         throw logging::error(funcname, "Errors were encountered during config reading.");
      default:
         throw logging::error(funcname, "cfg_parse() returned unexpected value");

      case CFG_SUCCESS: break;
   }   

   std::stringstream errors;
   read_cfg_section(&config, cfg, errors);
   cfg_free(cfg);

   errors.peek();
   if (!errors.eof())
   {
      for (std::string line; getline(errors, line); )
         logger.log_message(LOG_ERR, funcname, line.c_str());
      return 0;
   }
   return 1;
}

} // CONFIG NAMESPACE
