#include <vector>
#include <sstream>
#include <cstring>
#include <algorithm>

#include <confuse.h>

#include "aux_log.h"
#include "prog_config.h"

namespace configuration {

static std::unordered_map<std::type_index, std::vector<std::type_index>> allowed_types = {
   { std::type_index(typeid(config_map *)), { std::type_index(typeid(config_map *)) } },
   { std::type_index(typeid(int)),          { std::type_index(typeid(int)) } },
   { std::type_index(typeid(std::string)),  { std::type_index(typeid(std::string)), std::type_index(typeid(char *)) } }
};

void config_entry::check_type(const std::type_info &requested) const
{
   static const char *funcname = "config_entry::check_type";

   const std::type_info *current;

   switch (type)
   {
      case val_type::unknown:
         throw logging::error(funcname, "Attempt to access unknown-type config value.");
      default:
         throw logging::error(funcname, "Unexpected value type: %u", static_cast<int>(type));

      case val_type::section: current = &typeid(config_map *);  break;
      case val_type::string:  current = &typeid(std::string);   break;
      case val_type::integer: current = &typeid(int);        break;
   }

   for (auto &index : allowed_types[std::type_index(*current)])
      if (index == std::type_index(requested)) return;

   throw logging::error(funcname, "Request type mismatch with current value. Requested: %s.",
                        requested.name());
}

template <typename T>
void config_entry::set_value(const T& newval)
{
   static const char *funcname = "config_entry::set_value";

   check_type(typeid(T));
   if (locked) throw logging::error(funcname, "Attempt to set value on already set and locked entry.");

   locked = true;
   value = newval;
}

cfg_opt_t * build_section(config_map *section, std::vector<cfg_opt_t *> &ptrs)
{
   static const char *funcname = "configuration::build_section";

   cfg_opt_t *ptr = new cfg_opt_t[section->size() + 1];
   ptrs.push_back(ptr);

   for (auto &entry : *section)
   {
      char *name = const_cast<char *>(entry.first.c_str());

      switch (entry.second.what_type())
      {
         case val_type::string:
         {
            cfg_opt_t temp = CFG_STR(name, 0, CFGF_NODEFAULT);
            *ptr = temp;
            break;
         }

         case val_type::integer:
         {
            cfg_opt_t temp = CFG_INT(name, 0, CFGF_NODEFAULT);
            *ptr = temp;
            break;
         }

         case val_type::section:
         {
            cfg_opt_t temp = CFG_SEC(name, build_section(entry.second.map(), ptrs), CFGF_NONE);
            *ptr = temp;
            break;
         }

         case val_type::unknown:
            throw logging::error(funcname, "Val with unknown type in section: %s", name);
      }
      ptr++;
   }

   cfg_opt_t temp = CFG_END();
   *ptr = temp;
   return ptrs.back();
}

void read_cfg_section(config_map *section, cfg_t *cfg_sec, std::stringstream &errors)
{
   static const char *funcname = "configuration::read_cfg_section";

   for (auto &entry : *section)
   {
      char *name = const_cast<char *>(entry.first.c_str());
      bool required = entry.second.is_required();

      switch (entry.second.what_type())
      {
         case val_type::integer:
         {
            int val;
            if (0 == (val = cfg_getint(cfg_sec, name)) and required)
               errors << "Required int-option '" << name << "' is not set." << std::endl;
            else entry.second.set_value(val);
            break;
         }

         case val_type::string:
         {
            char *val;
            if (nullptr == (val = cfg_getstr(cfg_sec, name)) and required)
               errors << "Required str-option '" << name << "' is not set." << std::endl;
            else entry.second.set_value(val);
            break;
         }

         case val_type::section:
            read_cfg_section(entry.second.map(), cfg_getsec(cfg_sec, name), errors);
            break;

         case val_type::unknown:
            throw logging::error(funcname, "Val with unknown type in section: %s", name);   
      }
   }
}

void cfg_error_fnc(cfg_t *cfg, const char *format, va_list ap)
{
   logger.log_vmessage(LOG_ERR, "cfg_parse", format, ap);
}

int read_config(const char *filename, config_map &config)
{
   static const char *funcname = "configuration::read_config";

   // Assuming that we have at least one section.
   // This function won't be called if there is nothing to read, right?      
   std::vector<cfg_opt_t *> section_ptrs;
   build_section(&config, section_ptrs);

   cfg_t *cfg = cfg_init(section_ptrs[0], CFGF_NONE);
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
   std::for_each(section_ptrs.begin(), section_ptrs.end(), [](cfg_opt_t *x) { delete [] x; });
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
