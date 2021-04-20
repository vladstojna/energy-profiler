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


    class output_file
    {
    private:
        enum class tag;

        tag _tag;
        std::string _filename;
        std::ostream* _outstream;

    public:
        output_file(const std::string& file);
        output_file(std::string&& file);
        ~output_file();

        output_file(output_file&& other);
        output_file& operator=(output_file&& other);

        std::ostream& stream();
        const std::string& filename() const;

        explicit operator bool() const;

    private:
        void init();
    };


    class arguments
    {
    private:
        int _target_idx;
        bool _pie;
        bool _idle;
        output_file _outfile;
        std::string _config;

    public:
        arguments(int idx, bool pie, bool idle, output_file&& of, const std::string& cfg);
        arguments(int idx, bool pie, bool idle, output_file&& of, std::string&& cfg);

        int target_index() const;
        bool pie() const;
        bool idle() const;

        output_file& outfile();
        const output_file& outfile() const;

        const std::string& config() const;
    };

    std::ostream& operator<<(std::ostream& os, const output_file& of);
    std::ostream& operator<<(std::ostream& os, const arguments& a);

    cmmn::expected<arguments, arg_error> parse_arguments(int argc, char* const argv[]);

}
