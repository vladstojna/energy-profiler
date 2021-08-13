// main.cpp

#include <cstring>
#include <iostream>

#include "cmdargs.hpp"
#include "dbg.hpp"
#include "error.hpp"
#include "profiler.hpp"
#include "ptrace_wrapper.hpp"
#include "target.hpp"
#include "log.hpp"


int main(int argc, char* argv[])
{
    using namespace tep;
    std::optional<arguments> args = parse_arguments(argc, argv);
    if (!args)
        return 1;

    log::init(args.value().logargs.quiet, args.value().logargs.path);

    dbg_expected<dbg_info> dbg_info = dbg_info::create(args.value().target);
    if (!dbg_info)
    {
        std::cerr << dbg_info.error() << std::endl;
        return 1;
    }
    cfg_result config = load_config(args.value().config);
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
    pid_t child_pid = ptrace_wrapper::instance.fork(errnum, &run_target, args.value().argv);
    if (child_pid > 0)
    {
        cmmn::expected<profiler, tracer_error> profiler = profiler::create(child_pid,
            args.value().profiler_flags,
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

        args.value().output << results.value();
        return 0;
    }
    else if (child_pid == -1)
        log::logline(log::error, "fork(): %s", strerror(errnum));
    return 1;
}
