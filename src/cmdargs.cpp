// cmdargs.cpp

#include "cmdargs.hpp"

#include <cassert>
#include <cstring>
#include <iostream>
#include <iomanip>
#include <charconv>

#include <getopt.h>

using namespace tep;

extern int opterr;
extern int optopt;
extern int optind;
extern char* optarg;

namespace
{
    std::string_view cpu_sensors_str{ "cpu-sensors" };
    std::string_view cpu_sockets_str{ "cpu-sockets" };
    std::string_view gpu_devices_str{ "gpu-devices" };

    std::optional<unsigned long long>
        parse_mask_argument(std::string_view option, std::string_view value)
    {
        if (value == "all")
            return ~0x0ULL;
        unsigned long long retval;
        auto [ptr, ec] =
            std::from_chars(value.begin(), value.end(), retval, 16);
        if (auto err = std::make_error_code(ec))
        {
            std::cerr << "--" << option << ": " << err << "\n";
            return std::nullopt;
        }
        if (ptr != value.end())
        {
            std::cerr << "--" << option << ": "
                << "invalid hexadecimal characters in '" << value << "'" << "\n";
            return std::nullopt;
        }
        return retval;
    }
}

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

    auto pad = std::setw(30);
    std::ios::fmtflags flags(std::cout.flags());

    std::cout << std::left << pad << "-h, --help"
        << "print this message and exit"
        << "\n";

    std::cout << std::left << pad << "-c, --config <file>"
        << "(optional) read from configuration file <file>; "
        << "if <file> is 'stdin' then stdin is used (default: stdin)"
        << "\n";

    std::cout << std::left << pad << "-o, --output <file>"
        << "(optional) write profiling results to <file>; "
        << "if <file> is 'stdout' then stdout is used (default: stdout)"
        << "\n";

    std::cout << std::left << pad << "-q, --quiet"
        << "suppress log messages except errors to stderr (default: off)"
        << "\n";

    std::cout << std::left << pad << "-l, --log <file>"
        << "(optional) write log to <file> (default: stdout)"
        << "\n";

    std::cout << std::left << pad << "--idle"
        << "gather idle readings at startup (default)"
        << "\n";

    std::cout << std::left << pad << "--no-idle"
        << "opposite of --idle"
        << "\n";

    std::cout << std::left << pad << "--cpu-sensors {MASK,all}"
        << "mask of CPU sensors to read in hexadecimal, "
        << "overwrites config value (default: use value in config)"
        << "\n";

    std::cout << std::left << pad << "--cpu-sockets {MASK,all}"
        << "mask of CPU sockets to profile in hexadecimal, "
        << "overwrites config value (default: use value in config)"
        << "\n";

    std::cout << std::left << pad << "--gpu-devices {MASK,all}"
        << "mask of GPU devices to profile in hexadecimal, "
        << "overwrites config value (default: use value in config)"
        << "\n";

    std::cout.flush();
    std::cout.flags(flags);
}

std::optional<arguments> tep::parse_arguments(int argc, char* const argv[])
{
    int c;
    int option_index = 0;
    int idle = 1;
    bool quiet = false;
    std::string output;
    std::string config;
    std::string logpath;

    unsigned long long cpu_sensors = 0;
    unsigned long long cpu_sockets = 0;
    unsigned long long gpu_devices = 0;

    struct option long_options[] =
    {
        { "help",                 no_argument,       nullptr, 'h' },
        { "idle",                 no_argument,       &idle, 1 },
        { "no-idle",              no_argument,       &idle, 0 },
        { "config",               required_argument, nullptr, 'c' },
        { "output",               required_argument, nullptr, 'o' },
        { "quiet",                no_argument,       nullptr, 'q' },
        { "log",                  required_argument, nullptr, 'l' },
        { cpu_sensors_str.data(), required_argument, nullptr, 0 },
        { cpu_sockets_str.data(), required_argument, nullptr, 0 },
        { gpu_devices_str.data(), required_argument, nullptr, 0 },
        { nullptr,       0,                 nullptr, 0 }
    };

    while ((c = getopt_long(argc, argv, "hqc:o:l:", long_options, &option_index)) != -1)
    {
        switch (c)
        {
        case 0:
        {
            const char* opt = long_options[option_index].name;
            if (auto parsed_value = parse_mask_argument(opt, optarg))
            {
                std::cout << *parsed_value << "\n";
                if (opt == cpu_sensors_str)
                    cpu_sensors = *parsed_value;
                else if (opt == cpu_sockets_str)
                    cpu_sockets = *parsed_value;
                else if (opt == gpu_devices_str)
                    gpu_devices = *parsed_value;
                break;
            }
            return std::nullopt;
        }
        case 'c':
            config = optarg;
            break;
        case 'o':
            output = optarg;
            break;
        case 'l':
            logpath = optarg;
            break;
        case 'q':
            quiet = true;
            break;
        case 'h':
        case '?':
            // getopt already printed and error message
            print_usage(argv[0]);
            return std::nullopt;
        default:
            assert(false);
        }
    }

    if (optind == argc)
    {
        std::cerr << "missing target executable name\n";
        return std::nullopt;
    }

    if (quiet && !logpath.empty())
    {
        std::cerr << "both -q/--quiet and -l/--log provided\n";
        return std::nullopt;
    }

    optional_output_file of(output);
    optional_input_file cfg(config);

    if (!of)
    {
        std::cerr << "error opening output file '" << output << "': "
            << strerror(errno) << "\n";
        return std::nullopt;
    }
    if (!cfg)
    {
        std::cerr << "error opening config file '" << config << "': "
            << strerror(errno) << "\n";
        return std::nullopt;
    }

    return arguments{
        flags{ bool(idle), cpu_sensors, cpu_sockets, gpu_devices },
        std::move(config),
        std::move(of),
        log_args{ bool(quiet), std::move(logpath) },
        std::string(argv[optind]),
        &argv[optind]
    };
}
