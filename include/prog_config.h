#ifndef AUX_CONFIG_H
#define AUX_CONFIG_H

#include <unordered_map>
#include <typeinfo>
#include <typeindex>

#include <vector>
#include <string>

#include <boost/variant.hpp>

#include "typedef.h"

namespace conf {

using conf_en = class config_entry;
using config_map = std::unordered_map<std::string, conf_en>;

using integer_t = int;
using string_t = std::string;
using multistring_t = std::vector<std::string>;
using section_t = config_map;

enum class val_type : int {
   integer = 0,
   string,
   multistring,
   section,
   unknown = 254
};

class config_entry
{
   public:
      config_entry() : type(val_type::unknown), required(false), locked(false) { }
      
      // If there is no default we value, then we assume that is should be read from config.
      config_entry(val_type type_) : type(type_), required(true), locked(false) { }
      template <typename T> config_entry(val_type type_, const T& val) : 
         type(type_), required(false), locked(false) { value = val; }

      val_type what_type() const { return type; }
      bool is_required() const { return required; }

      size_t size() const { check_type(typeid(section_t *)); return (boost::get<section_t *>(value))->size(); }
      const config_entry& operator[](const std::string &key) const {
         check_type(typeid(section_t *)); return (*(boost::get<section_t *>(value)))[key]; }

      template <typename T> void set(const T& newval);
      template <typename T> const T& get() const { check_type(typeid(T)); return boost::get<T>(value); }

   private:
      val_type type;
      bool required;
      bool locked;

      boost::variant<integer_t, string_t, multistring_t, section_t *> value;

      void check_type(const std::type_info &requested) const;
};

int read_config(const char *filename, section_t &config);

} // CONFIG NAMESPACE

extern conf::section_t config;

#endif
