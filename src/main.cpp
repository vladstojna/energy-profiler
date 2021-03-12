// main.cpp

#include <fstream>
#include <iostream>
#include <unordered_set>

#include <unistd.h>

#include "cmdargs.h"
#include "energy_reader.h"
#include "dbg.hpp"
#include "macros.h"
#include "profiler.h"
#include "target.h"
#include "util.h"

std::unordered_set<uintptr_t> get_breakpoint_addresses(
    const tep::dbg_line_info& dbg_info,
    std::vector<tep::arguments::breakpoint>& breakpoints)
{
    std::unordered_set<uintptr_t> addresses;
    if (dbg_info.has_dbg_symbols())
    {
        addresses.reserve(breakpoints.size());
        for (const auto& bp : breakpoints)
        {
            auto cu = dbg_info.find_cu(bp.cu_name);
            if (!cu)
                throw std::runtime_error(cu.error().message);
            auto addr = cu.value()->line_first_addr(bp.lineno);
            if (!addr)
                throw std::runtime_error(addr.error().message);
            addresses.insert(addr.value());
        }
    }
    else
        tep::procmsg("no debug symbols found; ignoring breakpoints\n");
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
            if (!outfile)
                throw std::runtime_error("unable to open " +
                    args.outfile + " for writing");

            tep::dbg_expected<tep::dbg_line_info> dbg_info = tep::dbg_line_info::create(argv[idx]);
            if (!dbg_info)
            {
                std::cerr << dbg_info.error() << std::endl;
                return 1;
            }
            std::cout << dbg_info.value() << std::endl;
            tep::profiler profiler(child_pid,
                outfile,
                std::chrono::milliseconds(args.interval),
                get_breakpoint_addresses(dbg_info.value(), args.breakpoints),
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
