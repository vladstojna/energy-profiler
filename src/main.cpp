// main.cpp

#include "cmdargs.hpp"
#include "dbg.hpp"
#include "error.hpp"
#include "profiler.hpp"
#include "ptrace_wrapper.hpp"
#include "target.hpp"
#include "log.hpp"

#include <cstring>
#include <iostream>

template<typename T, typename E>
using expected = nonstd::expected<T, E>;

int main(int argc, char* argv[])
{
    using namespace tep;

    std::optional<arguments> args = parse_arguments(argc, argv);
    if (!args)
        return 1;

    log::init(args->logargs.quiet, args->logargs.path);

    dbg_expected<dbg_info> dbg_info = dbg_info::create(args->target);
    if (!dbg_info)
    {
        std::cerr << dbg_info.error() << std::endl;
        return 1;
    }

    auto config = cfg::config_t::create(args->config);
    if (!config)
    {
        std::cerr << config.error() << std::endl;
        return 1;
    }

#ifndef NDEBUG
    std::cout << *args << "\n";
    std::cout << *dbg_info << "\n";
    std::cout << *config << std::endl;
#endif

    int errnum;
    pid_t child_pid = ptrace_wrapper::instance.fork(errnum, &run_target, args->argv);
    if (child_pid > 0)
    {
        expected<profiler, tracer_error> profiler =
            profiler::create(
                child_pid,
                (*args).profiler_flags,
                std::move(*dbg_info),
                std::move(*config));
        if (!profiler)
        {
            std::cerr << profiler.error() << std::endl;
            return 1;
        }

        if (!args->same_target())
        {
            if (auto err = profiler->await_executable(args->target))
            {
                std::cerr << err << std::endl;
                return 1;
            }
        }

        expected<profiling_results, tracer_error> results = profiler->run();
        if (!results)
        {
            std::cerr << results.error() << std::endl;
            return 1;
        }

        (*args).output << *results;
        return 0;
    }
    else if (child_pid == -1)
        log::logline(log::error, "fork(): %s", strerror(errnum));
    return 1;
}
