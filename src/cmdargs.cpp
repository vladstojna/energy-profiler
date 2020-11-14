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
    cu_name(nm),
    lineno(ln)
{
}

tep::arguments::breakpoint::breakpoint(breakpoint&& other) = default;

tep::arguments::arguments() :
    interval(1500),
    breakpoints(),
    quiet(false),
    regular(false),
    delta(false)
{
}

tep::arguments::arguments(arguments && other) = default;

std::ostream& tep::operator<<(std::ostream & os, const arguments & args)
{
    os << "interval=" << args.interval << "\n";
    os << "quiet=" << (args.quiet ? "true\n" : "false\n");
    os << "regular=" << (args.regular ? "true\n" : "false\n");
    os << "delta=" << (args.delta ? "true\n" : "false\n");
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
    std::cout << profiler_name << " [options] -- [executable] [executable-args]\n\n";
    std::cout << "options:\n\n";
    std::cout << "-i, --interval <arg>    set energy consumption sampling interval to <arg> ms (default: 1500)\n";
    std::cout << "                        may introduce a big overhead when <arg> is too low\n";
    std::cout << "-q, --quiet             run profiler in quiet mode (default: off)\n";
    std::cout << "-o, --output <file>     output energy consumption results to <file> (default: stdout)\n";
    std::cout << "-h, --help              show this message\n";
    std::cout << "-b <cu>:<ln>            set breakpoint at line <ln> of compilation unit <cu>\n";
    std::cout << "-b <ln>                 same as above but unspecified CU; use only when dealing with a single CU\n";
    std::cout << std::endl;
}

int tep::parse_arguments(int argc, char* const argv[], arguments & args)
{
    long result;

    opterr = 0;

    int c;
    while ((c = getopt(argc, argv, "hi:b:")) != -1)
    {
        switch (c)
        {
        case 'i':
            result = strtol(optarg, NULL, 10);
            if (result <= 0 || result >= std::numeric_limits<uint32_t>::max())
            {
                fprintf(stderr, "Option -%c requires an unsigned 32-bit integer >0 (found '%s').\n",
                    c, optarg);
                return -1;
            }
            args.interval = result;
            break;
        case 'b':
        {
            std::cmatch mresults;
            bool matched = std::regex_match(optarg, mresults, std::regex(R"((.*?):(\d+))"));
            if (!matched) // full match + captures
            {
                fprintf(stderr, "Option -%c requires the following format: <cu>:<ln> or <ln>\n", c);
                fprintf(stderr, "where <cu> is the compilation unit and <ln> is the line number\n");
                fprintf(stderr, "(found '%s').\n", optarg);
                return -1;
            }
            std::string lineno_match = mresults.str(2);
            result = strtol(lineno_match.c_str(), NULL, 10);
            if (result <= 0 || result >= std::numeric_limits<uint32_t>::max())
            {
                fprintf(stderr, "Option -%c requires <ln> to be positive (found '%s')\n", c, lineno_match.c_str());
                return -1;
            }
            args.breakpoints.emplace_back(mresults.str(1), result);
        }
        break;
        case 'h':
            print_usage(argv[0]);
            return -1;
        case '?':
            if (optopt == 'i')
                fprintf(stderr, "Option -%c requires an argument.\n", optopt);
            else if (isprint(optopt))
                fprintf(stderr, "Unknown option '-%c'.\n", optopt);
            else
                fprintf(stderr, "Unknown option character '\\x%x'.\n", optopt);
            return -1;
        default:
            abort();
        }
    }

    if (optind >= 0 && argv[optind] == nullptr)
    {
        fprintf(stderr, "Missing executable name\n");
        print_usage(argv[0]);
        return -1;
    }

    return optind;
}
