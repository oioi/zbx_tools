#ifndef ZBX_MAILER_H
#define ZBX_MAILER_H

#include <string>
#include <boost/regex.hpp>

#include "typedef.h"

using tex_func = void (*) (std::string &arg, std::string &line);

struct regexes
{
   boost::regex expr;
   tex_func process;
   uint_t arg_offset;

   regexes(const char *str, tex_func proc, uint_t offset) : 
      expr(str), process(proc), arg_offset(offset) { }
};

void insert_graphid_img(std::string &arg, std::string &line);
void insert_itemgraph_img(std::string &arg, std::string &line);
void replace_peername(std::string &arg, std::string &line);

#endif
