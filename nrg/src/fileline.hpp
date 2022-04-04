#pragma once

#include "visibility.hpp"

#include <string>

namespace nrgprf {
// formats a string as 'file:line: msg' and returns formatted string
NRG_LOCAL std::string fileline__(const char *file, int line,
                                 const std::string &msg);
NRG_LOCAL std::string fileline__(const char *file, int line, const char *msg);

#define fileline(arg) fileline__(__FILE__, __LINE__, arg)
} // namespace nrgprf
