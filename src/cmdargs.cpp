// cmdargs.cpp
#include "cmdargs.h"

#include <iostream>
#include <regex>
#include <limits>

#include <getopt.h>

extern int opterr;
extern int optopt;
extern int optind;
extern char* optarg;

tep::arguments::breakpoint::breakpoint(const std::string& nm, uint32_t ln) :
    cu_name(nm),
    lineno(ln)
{
}

tep::arguments::breakpoint::breakpoint(const char* nm, uint32_t ln) :
    cu_name(nm),
    lineno(ln)
{
}

tep::arguments::breakpoint::breakpoint(std::string&& nm, uint32_t ln) :
    cu_name(std::move(nm)),
    lineno(ln)
{
}

tep::arguments::breakpoint::breakpoint(breakpoint&& other) = default;

tep::arguments::arguments() :
    interval(1000),
    outfile(),
    target("smp"),
    breakpoints(),
    quiet(false)
{
}

tep::arguments::arguments(arguments && other) = default;

std::ostream& tep::operator<<(std::ostream & os, const arguments & args)
{
    os << "interval=" << args.interval << "\n";
    os << "outfile=" << args.outfile << "\n";
    os << "target=" << args.target << "\n";
    os << "quiet=" << (args.quiet ? "true\n" : "false\n");
    os << "breakpoints:";
    for (const auto& bp : args.breakpoints)
    {
        os << " " << bp.cu_name << ":" << bp.lineno;
    }
    os << "\n";
    return os;
}

void print_usage(const char* profiler_name)
{
    std::cout << "Usage:\n\n";
    std::cout << profiler_name << " [options] [--] [executable] [executable-args]\n\n";
    std::cout << "options:\n\n";
    std::cout << "-i, --interval <arg>           set energy consumption sampling interval to <arg> ms (default: 1000)\n";
    std::cout << "                               may introduce measurable overhead when <arg> is too low\n";
    std::cout << "-q, --quiet                    run profiler in quiet mode (default: off)\n";
    std::cout << "-o, --output <file>            output energy consumption results to <file> (default: stdout)\n";
    std::cout << "                               if <file> is - then stdout is used\n";
    std::cout << "-t, --target smp/gpu/fpga      which hardware component to measure energy consumption from\n";
    std::cout << "-h, --help                     show this message\n";
    std::cout << "-b, --breakpoint <cu>:<ln>     set breakpoint on line <ln> of compilation unit <cu>\n";
    std::cout << "-b, --breakpoint :<ln>         same as above but unspecified CU; use only when dealing with a single CU\n";
    std::cout << std::endl;
}

int tep::parse_arguments(int argc, char* const argv[], arguments & args)
{
    struct option long_options[] =
    {
        {"help",       no_argument,       0, 'h'},
        {"interval",   required_argument, 0, 'i'},
        {"target",     required_argument, 0, 't'},
        {"output",     required_argument, 0, 'o'},
        {"breakpoint", required_argument, 0, 'b'},
        {0, 0, 0, 0}
    };

    long result;
    int c;
    int option_index = 0;
    while ((c = getopt_long(argc, argv, "ht:i:o:b:", long_options, &option_index)) != -1)
    {
        switch (c)
        {
        case 'i':
            result = strtol(optarg, NULL, 10);
            if (result <= 0 || result >= std::numeric_limits<uint32_t>::max())
            {
                fprintf(stderr, "Option -%c/--%s requires an unsigned 32-bit integer >0 (found '%s').\n",
                    c, long_options[option_index].name, optarg);
                return -1;
            }
            args.interval = result;
            break;
        case 'b':
        {
            std::cmatch mresults;
            bool matched = std::regex_match(optarg, mresults, std::regex(R"((.*?):(\d+))"));
            if (!matched || mresults.size() < 3) // full match + captures
            {
                fprintf(stderr, "Option -%c/--%s requires the following format:"
                    "<cu>:<ln> or :<ln> where <ln> is greater than zero (found '%s').\n",
                    c, long_options[option_index].name, optarg);
                return -1;
            }
            std::string lineno_match(mresults.str(2));
            result = strtol(lineno_match.c_str(), NULL, 10);
            if (result <= 0 || result >= std::numeric_limits<uint32_t>::max())
            {
                fprintf(stderr, "Option -%c/--%s requires <ln> to be positive (found '%ld')\n",
                    c, long_options[option_index].name, result);
                return -1;
            }
            args.breakpoints.emplace_back(mresults.str(1), result);
            break;
        }
        case 'o':
            if (strcmp(optarg, "-") != 0)
                args.outfile = optarg;
            break;
        case 't':
            args.target = optarg;
            break;
        case 'h':
        case '?':
            // getopt already printed and error message
            print_usage(argv[0]);
            return -1;
        default:
            abort();
        }
    }

    if (optind == argc)
    {
        fprintf(stderr, "Missing executable name\n");
        print_usage(argv[0]);
        return -1;
    }
    return optind;
}
