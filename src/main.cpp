// main.cpp

#include <fstream>
#include <iostream>
#include <unordered_set>

#include <unistd.h>

#include "cmdargs.h"
#include "energy_reader.h"
#include "dbg.h"
#include "macros.h"
#include "profiler.h"
#include "target.h"
#include "util.h"

std::unordered_set<tep::line_addr> get_breakpoint_addresses(
    const tep::dbg_line_info& dbg_info,
    std::vector<tep::arguments::breakpoint>& breakpoints)
{
    std::unordered_set<tep::line_addr> addresses;
    if (dbg_info.has_dbg_symbols())
    {
        addresses.reserve(breakpoints.size());
        for (const auto& bp : breakpoints)
        {
            tep::line_addr addr = dbg_info
                .cu_by_name(bp.cu_name)
                .line_first_addr(bp.lineno);
            addresses.emplace(addr);
        }
    }
    else
    {
        tep::procmsg("no debug symbols found; ignoring breakpoints\n");
    }
    return addresses;
}

int main(int argc, char* argv[])
{
    tep::arguments args;
    int idx = tep::parse_arguments(argc, argv, args);
    if (idx < 0)
    {
        return 1;
    }
    dbg(std::cout << args << "\n");

    pid_t child_pid = fork();
    if (child_pid == 0)
    {
        tep::run_target(&argv[idx]);
    }
    else if (child_pid > 0)
    {
        try
        {
            std::ofstream os;
            std::ostream& outfile = args.outfile.empty() ?
                std::cout : (os = std::ofstream(args.outfile));

            tep::dbg_line_info dbg_info(argv[idx]);
            tep::profiler profiler(child_pid,
                outfile,
                std::chrono::milliseconds(args.interval),
                get_breakpoint_addresses(dbg_info, args.breakpoints),
                tep::make_energy_reader(
                    tep::energy::target::smp,
                    tep::energy::engine::papi));

            profiler.run();
            return 0;
        }
        catch (const std::exception& e)
        {
            fprintf(stderr, "%s\n", e.what());
        }
    }
    else
    {
        perror(fileline("fork"));
    }
    return 1;
}
