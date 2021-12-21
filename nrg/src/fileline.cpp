#include "fileline.hpp"

#include <cassert>
#include <cstdio>

template<typename T>
std::string fileline_general(const char* file, int line, T&& msg)
{
    char buff[256];
    int sz = snprintf(buff, 256, "%s:%d: ", file, line);
    assert(sz > 0);
    std::string result(buff, sz);
    result.append(std::forward<T>(msg));
    return result;
}

std::string nrgprf::fileline__(const char* file, int line, const std::string& msg)
{
    return fileline_general(file, line, msg);
}

std::string nrgprf::fileline__(const char* file, int line, const char* msg)
{
    return fileline_general(file, line, msg);
}
