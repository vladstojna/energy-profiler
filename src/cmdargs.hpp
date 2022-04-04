// cmdargs.h

#pragma once

#include "flags.hpp"

#include <fstream>
#include <iosfwd>
#include <string>
#include <optional>

namespace tep
{
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

    struct log_args
    {
        bool quiet;
        std::string path;
    };

    struct arguments
    {
        flags profiler_flags;
        optional_input_file config;
        optional_output_file output;
        std::ofstream debug_dump;
        log_args logargs;
        std::string target;
        char* const* argv;

        bool same_target() const;
    };

    std::ostream& operator<<(std::ostream& os, const optional_output_file& f);
    std::ostream& operator<<(std::ostream& os, const optional_input_file& f);
    std::ostream& operator<<(std::ostream& os, const arguments& a);

    std::optional<arguments> parse_arguments(int argc, char* const argv[]);
}
