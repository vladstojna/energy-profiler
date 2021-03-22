// cmdargs.h

#pragma once

#include <string>
#include <fstream>
#include <iosfwd>

#include <expected.hpp>

namespace tep
{

    struct arg_error
    {};

    class arguments
    {
    private:
        int _target_idx;
        std::string _outfile;
        std::string _config;

    public:
        arguments(int idx, const std::string& out, const std::string& cfg);
        arguments(int idx, std::string&& out, const std::string& cfg);
        arguments(int idx, const std::string& out, std::string&& cfg);
        arguments(int idx, std::string&& out, std::string&& cfg);

        int target_index() const { return _target_idx; }
        const std::string& outfile() const { return _outfile; }
        const std::string& config() const { return _config; }
    };

    std::ostream& operator<<(std::ostream& os, const arg_error& e);
    std::ostream& operator<<(std::ostream& os, const arguments& a);

    cmmn::expected<arguments, arg_error> parse_arguments(int argc, char* const argv[]);

}
