// util.hpp

#pragma once

#include <string>

namespace nrgprf
{

    // formats a string as 'file:line: msg' and returns formatted string
    std::string fileline__(const char* file, int line, const std::string& msg);
    std::string fileline__(const char* file, int line, const char* msg);

#define fileline(arg) \
    fileline__(__FILE__, __LINE__, arg)

}
