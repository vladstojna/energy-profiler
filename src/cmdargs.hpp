// cmdargs.h

#pragma once

#include "flags.hpp"

#include <fstream>
#include <iosfwd>
#include <string>

#include <util/expected.hpp>

namespace tep
{
    struct arg_error
    {};

    class optional_output_file
    {
        std::ofstream _file;

    public:
        optional_output_file(const std::string& path = "");
        operator std::ostream& ();
        explicit operator bool() const;
        friend std::ostream& operator<<(std::ostream&, const optional_output_file&);
    };

    class optional_input_file
    {
        std::ifstream _file;

    public:
        optional_input_file(const std::string& path = "");
        operator std::istream& ();
        explicit operator bool() const;
        friend std::ostream& operator<<(std::ostream&, const optional_input_file&);
    };

    struct arguments
    {
        flags profiler_flags;
        optional_input_file config;
        optional_output_file output;
        std::string target;
        char* const* argv;
    };

    std::ostream& operator<<(std::ostream& os, const optional_output_file& f);
    std::ostream& operator<<(std::ostream& os, const optional_input_file& f);
    std::ostream& operator<<(std::ostream& os, const arguments& a);

    cmmn::expected<arguments, arg_error> parse_arguments(int argc, char* const argv[]);

}
