#ifndef AUX_CONFIG_H
#define AUX_CONFIG_H

#include <unordered_map>
#include <typeinfo>
#include <typeindex>
#include <string>

#include <boost/variant.hpp>

#include "typedef.h"

namespace configuration {

using conf_en = class config_entry;
using config_map = std::unordered_map<std::string, conf_en>;   

enum class val_type : int {
   unknown = 0,
   section,
   integer,
   string
};

class config_entry
{
   public:
      config_entry() : type(val_type::unknown), required(false), locked(false) { value = 0; }
      config_entry(bool req_, int int_) : type(val_type::integer), required(req_), locked(false) { value = int_; }
      config_entry(bool req_, config_map *map_): type(val_type::section), required(req_), locked(false) { value = map_; }
      config_entry(bool req_, const char *str_) : type(val_type::string), required(req_), locked(false) { 
         if (nullptr != str_) value = str_; }

      val_type what_type() const { return type; }
      bool is_required() const { return required; }

      size_t size() const { check_type(typeid(config_map *)); return (boost::get<config_map *>(value))->size(); }
      const config_entry& operator[](const std::string &key) const {
         check_type(typeid(config_map *)); return (*(boost::get<config_map *>(value)))[key]; }

      config_map* map() const { check_type(typeid(config_map *)); return boost::get<config_map *>(value); }
      const std::string& str() const { check_type(typeid(std::string)); return boost::get<std::string>(value); }
      int intv() const { check_type(typeid(int)); return boost::get<int>(value); }

      template <typename T> void set_value(const T& newval);


   private:
      val_type type;
      bool required;
      bool locked;
      boost::variant<config_map *, int, std::string> value;

      void check_type(const std::type_info &requested) const;
};

int read_config(const char *filename, config_map &config);

} // CONFIG NAMESPACE

extern configuration::config_map config;

#endif
