// cmdargs.cpp

#include "cmdargs.hpp"

#include <iostream>

#include <getopt.h>

using namespace tep;

extern int opterr;
extern int optopt;
extern int optind;
extern char* optarg;

arguments::arguments(int idx, const std::string& out, const std::string& cfg) :
    _target_idx(idx),
    _outfile(out),
    _config(cfg)
{}

arguments::arguments(int idx, std::string&& out, const std::string& cfg) :
    _target_idx(idx),
    _outfile(std::move(out)),
    _config(cfg)
{}

arguments::arguments(int idx, const std::string& out, std::string&& cfg) :
    _target_idx(idx),
    _outfile(out),
    _config(std::move(cfg))
{}

arguments::arguments(int idx, std::string&& out, std::string&& cfg) :
    _target_idx(idx),
    _outfile(std::move(out)),
    _config(std::move(cfg))
{}

std::ostream& tep::operator<<(std::ostream& os, const arguments& args)
{
    os << "target @ index " << args.target_index();
    os << ", output: " << (args.outfile().empty() ? "stdout" : args.outfile());
    os << ", config file: " << args.config();
    return os;
}

void print_usage(const char* profiler_name)
{
    std::cout << "Usage:\n\n";
    std::cout << profiler_name << " [options] [--] [executable] [executable-args]\n\n";
    std::cout << "options:\n\n";
    std::cout << "-c, --config <file> required: read from configuration file <file>\n";
    std::cout << "                    must be provided and cannot be empty\n";
    std::cout << "-o, --output <file> optional: write profiling results to <file> (default: stdout)\n";
    std::cout << "                    if <file> is - then stdout is used\n";
    std::cout << "                    if <file> is -- then stderr is used\n";
    std::cout << "-h, --help          print this message\n";
    std::cout << std::endl;
}

cmmn::expected<arguments, arg_error> tep::parse_arguments(int argc, char* const argv[])
{
    struct option long_options[] =
    {
        { "help", no_argument, 0, 'h' },
        { "config", required_argument, 0, 'c' },
        { "output", required_argument, 0, 'o' },
        {0, 0, 0, 0}
    };

    int c;
    int option_index = 0;
    std::string output;
    std::string config;
    while ((c = getopt_long(argc, argv, "hc:o:", long_options, &option_index)) != -1)
    {
        switch (c)
        {
        case 'c':
            if (*optarg)
                config = optarg;
            break;
        case 'o':
            if (*optarg)
                output = optarg;
            break;
        case 'h':
        case '?':
            // getopt already printed and error message
            print_usage(argv[0]);
            return arg_error();
        default:
            abort();
        }
    }

    if (optind == argc)
    {
        std::cerr << "missing target executable name\n";
        return arg_error();
    }

    if (config.empty())
    {
        std::cerr << "configuration file name cannot be empty\n";
        return arg_error();
    }

    return { optind, std::move(output), std::move(config) };
}
