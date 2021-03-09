// util.hpp

#pragma once

#include <string>

namespace nrgprf
{

    // formats a string as 'file@line: msg' and returns formatted string
    std::string fileline(const char* file, int line, const std::string& msg);
    std::string fileline(const char* file, int line, const char* msg);

    // formats a string as 'file@line: msg' and returns true if output fit into buffer
    // or false if output was truncated
    bool fileline(char* out, size_t outsz, const char* file, int line, const char* msg);

}
