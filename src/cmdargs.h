// cmdargs.h
#pragma once

#include <vector>
#include <string>
#include <iosfwd>

namespace tep
{

struct arguments
{
    struct breakpoint
    {
        std::string cu_name;
        uint32_t lineno;

        breakpoint(const std::string& nm, uint32_t ln);
        breakpoint(const char* nm, uint32_t ln);
        breakpoint(std::string&& nm, uint32_t ln);
        breakpoint(breakpoint&& other);

        breakpoint(const breakpoint& other) = delete;
        breakpoint& operator=(const breakpoint& other) = delete;
    };

    uint32_t interval;
    std::vector<breakpoint> breakpoints;
    bool quiet;
    bool regular;
    bool delta;

    arguments();
    arguments(arguments&& other);

    arguments(const arguments& other) = delete;
    arguments& operator=(const arguments& other) = delete;
};

std::ostream& operator<<(std::ostream& os, const arguments& args);

int parse_arguments(int argc, char* const argv[], arguments& args);

}
