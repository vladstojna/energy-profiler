// cmdargs.cpp

#include "cmdargs.hpp"

#include <cassert>
#include <cstring>
#include <iostream>
#include <getopt.h>

using namespace tep;

extern int opterr;
extern int optopt;
extern int optind;
extern char* optarg;

optional_output_file::optional_output_file(const std::string& path) :
    _file()
{
    if (!path.empty())
        _file.open(path);
}

optional_output_file::operator bool() const
{
    return bool(_file);
}

optional_output_file::operator std::ostream& ()
{
    if (_file && !_file.is_open())
        return std::cout;
    return _file;
}

optional_input_file::optional_input_file(const std::string& path) :
    _file()
{
    if (!path.empty())
        _file.open(path);
}

optional_input_file::operator bool() const
{
    return bool(_file);
}

optional_input_file::operator std::istream& ()
{
    if (_file && !_file.is_open())
        return std::cin;
    return _file;
}

std::ostream& tep::operator<<(std::ostream& os, const optional_output_file& f)
{
    if (!f)
        os << "failed to open";
    else if (f._file.is_open())
        os << "file";
    else
        os << "stdout";
    return os;
}

std::ostream& tep::operator<<(std::ostream& os, const optional_input_file& f)
{
    if (!f)
        os << "failed to open";
    else if (f._file.is_open())
        os << "file";
    else
        os << "stdin";
    return os;
}

std::ostream& tep::operator<<(std::ostream& os, const arguments& args)
{
    os << "flags: " << args.profiler_flags;
    os << ", output: " << args.output;
    os << ", config: " << args.config;
    return os;
}

void print_usage(const char* profiler_name)
{
    std::cout << "Usage:\n\n";
    std::cout << profiler_name << " [options] [--] [executable] [executable-args]\n\n";
    std::cout << "options:\n\n";
    std::cout << "-h, --help            print this message\n";
    std::cout << "\n";
    std::cout << "-c, --config <file>   (optional) read from configuration file <file>\n";
    std::cout << "                      if <file> is 'stdin' then stdin is used\n";
    std::cout << "                      cannot be empty\n";
    std::cout << "                      (default: stdin)\n";
    std::cout << "\n";
    std::cout << "-o, --output <file>   (optional) write profiling results to <file>\n";
    std::cout << "                      if <file> is 'stdout' then stdout is used\n";
    std::cout << "                      cannot be empty\n";
    std::cout << "                      (default: stdout)\n";
    std::cout << "\n";
    std::cout << "--idle                (default) gather idle readings at startup\n";
    std::cout << "--no-idle             do not gather idle readings at startup\n";
    std::cout << std::endl;
}

cmmn::expected<arguments, arg_error> tep::parse_arguments(int argc, char* const argv[])
{
    int c;
    int option_index = 0;
    int idle = 1;
    std::string output;
    std::string config;

    struct option long_options[] =
    {
        { "help", no_argument, 0, 'h' },
        { "idle", no_argument, &idle, 1},
        { "no-idle", no_argument, &idle, 0},
        { "config", required_argument, 0, 'c' },
        { "output", required_argument, 0, 'o' },
        {0, 0, 0, 0}
    };

    while ((c = getopt_long(argc, argv, "hc:o:", long_options, &option_index)) != -1)
    {
        switch (c)
        {
        case 0:
            // empty
            break;
        case 'c':
            config = optarg;
            break;
        case 'o':
            output = optarg;
            break;
        case 'h':
        case '?':
            // getopt already printed and error message
            print_usage(argv[0]);
            return arg_error();
        default:
            assert(false);
        }
    }

    if (optind == argc)
    {
        std::cerr << "missing target executable name\n";
        return arg_error();
    }

    optional_output_file of(output);
    optional_input_file cfg(config);

    if (!of)
    {
        std::cerr << "error opening output file '" << output << "': "
            << strerror(errno) << "\n";
        return arg_error();
    }
    if (!cfg)
    {
        std::cerr << "error opening config file '" << config << "': "
            << strerror(errno) << "\n";
        return arg_error();
    }

    return arguments{
        flags(idle),
        std::move(config),
        std::move(of),
        std::string(argv[optind]),
        &argv[optind]
    };
}
