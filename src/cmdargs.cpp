// cmdargs.cpp

#include "cmdargs.hpp"

#include <cassert>
#include <iostream>
#include <getopt.h>

using namespace tep;

extern int opterr;
extern int optopt;
extern int optind;
extern char* optarg;

enum class output_file::tag
{
    file,
    stdstream
};

void output_file::init()
{
    if (_filename.empty() || _filename == "stdout")
    {
        _tag = tag::stdstream;
        _outstream = &std::cout;
    }
    else if (_filename == "stderr")
    {
        _tag = tag::stdstream;
        _outstream = &std::cerr;
    }
    else
    {
        _tag = tag::file;
        _outstream = new std::ofstream(_filename);
    }
}

output_file::output_file(const std::string& file) :
    _tag(tag::file),
    _filename(file),
    _outstream(nullptr)
{
    init();
}

output_file::output_file(std::string&& file) :
    _tag(tag::file),
    _filename(std::move(file)),
    _outstream(nullptr)
{
    init();
}

output_file::~output_file()
{
    if (_tag == tag::file)
        delete _outstream;
}

output_file::output_file(output_file&& other) :
    _tag(std::exchange(other._tag, tag::stdstream)),
    _filename(std::move(other._filename)),
    _outstream(std::exchange(other._outstream, nullptr))
{}

output_file& output_file::operator=(output_file&& other)
{
    _tag = std::exchange(other._tag, tag::stdstream);
    _filename = std::move(other._filename);
    _outstream = std::exchange(other._outstream, nullptr);
    return *this;
}

std::ostream& output_file::stream()
{
    return *_outstream;
}

const std::string& output_file::filename() const
{
    return _filename;
}

output_file::operator bool() const
{
    if (_outstream == nullptr)
        return false;
    return bool(*_outstream);
}

arguments::arguments(int idx, const flags& flags, output_file&& of, const std::string& cfg) :
    _target_idx(idx),
    _flags(flags),
    _outfile(std::move(of)),
    _config(cfg)
{}

arguments::arguments(int idx, const flags& flags, output_file&& of, std::string&& cfg) :
    _target_idx(idx),
    _flags(flags),
    _outfile(std::move(of)),
    _config(std::move(cfg))
{}

int arguments::target_index() const
{
    return _target_idx;
}

const flags& arguments::get_flags() const
{
    return _flags;
}

output_file& arguments::outfile()
{
    return _outfile;
}

const output_file& arguments::outfile() const
{
    return _outfile;
}

const std::string& arguments::config() const
{
    return _config;
}

std::ostream& tep::operator<<(std::ostream& os, const output_file& of)
{
    return os << (of.filename().empty() ? "stdout" : of.filename());
}

std::ostream& tep::operator<<(std::ostream& os, const arguments& args)
{
#ifndef NDEBUG
    os << "target @ index " << args.target_index() << "\n";
#endif
    os << "flags:\n" << args.get_flags();
    os << "\noutput to: " << args.outfile();
    os << "\nconfig file: " << args.config();
    return os;
}

void print_usage(const char* profiler_name)
{
    std::cout << "Usage:\n\n";
    std::cout << profiler_name << " [options] [--] [executable] [executable-args]\n\n";
    std::cout << "options:\n\n";
    std::cout << "-h, --help            print this message\n";
    std::cout << "\n";
    std::cout << "-c, --config <file>   (required) read from configuration file <file>\n";
    std::cout << "                      cannot be empty\n";
    std::cout << "\n";
    std::cout << "-o, --output <file>   (optional) write profiling results to <file>\n";
    std::cout << "                      if <file> is 'stdout' then stdout is used\n";
    std::cout << "                      if <file> is 'stderr' then stderr is used\n";
    std::cout << "                      (default: stdout)\n";
    std::cout << "\n";
    std::cout << "--pie                 (default) the target executable is a position\n";
    std::cout << "                      independent executable (PIE)\n";
    std::cout << "--no-pie              the target executable is not a position\n";
    std::cout << "                      independent executable\n";
    std::cout << "\n";
    std::cout << "--idle                (default) gather idle readings at startup\n";
    std::cout << "--no-idle             do not gather idle readings at startup\n";
    std::cout << std::endl;
}

cmmn::expected<arguments, arg_error> tep::parse_arguments(int argc, char* const argv[])
{
    int c;
    int option_index = 0;

    int pie = 1;
    int idle = 1;
    std::string output;
    std::string config;

    struct option long_options[] =
    {
        { "help", no_argument, 0, 'h' },
        { "pie", no_argument, &pie, 1 },
        { "no-pie", no_argument, &pie, 0 },
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

    output_file of(std::move(output));
    if (!of)
    {
        std::cerr << "error opening output file " << of << "\n";
        return arg_error();
    }

    if (config.empty())
    {
        std::cerr << "configuration file name cannot be empty\n";
        return arg_error();
    }

    return { optind, flags(idle) , std::move(of), std::move(config) };
}
