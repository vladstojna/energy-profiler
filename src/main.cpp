// main.cpp

#include <cstring>
#include <iostream>

#include "cmdargs.hpp"
#include "dbg.hpp"
#include "error.hpp"
#include "profiler.hpp"
#include "profiling_results.hpp"
#include "ptrace_wrapper.hpp"
#include "target.hpp"
#include "util.hpp"


int main(int argc, char* argv[])
{
    using namespace tep;
    cmmn::expected<arguments, arg_error> args = parse_arguments(argc, argv);
    if (!args)
        return 1;

    int idx = args.value().target_index();

    dbg_expected<dbg_info> dbg_info = dbg_info::create(argv[idx]);
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

#ifndef NDEBUG
    std::cout << args.value() << "\n";
    std::cout << dbg_info.value() << "\n";
    std::cout << config.value() << std::endl;
#endif

    int errnum;
    pid_t child_pid = ptrace_wrapper::instance.fork(errnum, &run_target, &argv[idx]);
    if (child_pid > 0)
    {
        cmmn::expected<profiler, tracer_error> profiler = profiler::create(child_pid,
            args.value().get_flags(),
            std::move(dbg_info.value()), std::move(config.value()));
        if (!profiler)
        {
            std::cerr << profiler.error() << std::endl;
            return 1;
        }

        cmmn::expected<profiling_results, tracer_error> results = profiler.value().run();
        if (!results)
        {
            std::cerr << results.error() << std::endl;
            return 1;
        }

        args.value().outfile().stream() << results.value();
        return 0;
    }
    else if (child_pid == -1)
    {
        log(log_lvl::error, "fork(): %s", strerror(errnum));
    }
    return 1;
}
