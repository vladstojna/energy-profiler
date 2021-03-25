// main.cpp

#include <cstring>
#include <fstream>
#include <iostream>

#include "cmdargs.hpp"
#include "dbg.hpp"
#include "error.hpp"
#include "profiler.h"
#include "ptrace_wrapper.hpp"
#include "target.h"
#include "util.h"

int main(int argc, char* argv[])
{
    using namespace tep;
    cmmn::expected<arguments, arg_error> args = parse_arguments(argc, argv);
    if (!args)
        return 1;

    int idx = args.value().target_index();

    dbg_expected<dbg_line_info> dbg_info = dbg_line_info::create(argv[idx]);
    if (!dbg_info)
    {
        std::cerr << dbg_info.error() << std::endl;
        return 1;
    }

    cfg_result config = load_config(args.value().config());
    if (!config)
    {
        std::cerr << config.error() << std::endl;
        return 1;
    }

    std::cout << args.value() << "\n";
    std::cout << dbg_info.value() << "\n";
    std::cout << config.value() << std::endl;

    int errnum;
    pid_t child_pid = ptrace_wrapper::instance.fork(errnum, &run_target, &argv[idx]);
    if (child_pid > 0)
    {
        profiler profiler(child_pid, std::move(dbg_info.value()), std::move(config.value()));
        cmmn::expected<profiling_results, tracer_error> results = profiler.run();
        if (!results)
        {
            std::cerr << results.error() << std::endl;
            return 1;
        }
        return 0;
    }
    else if (child_pid == -1)
    {
        log(log_lvl::error, "fork(): %s", strerror(errnum));
    }
    return 1;
}
