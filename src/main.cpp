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

void handle_exception()
{
    try
    {
        throw;
    }
    catch (const nrgprf::exception& e)
    {
        std::cerr << "NRG exception: " << e.what() << "\n";
    }
    catch (const tep::cfg::exception& e)
    {
        std::cerr << "Config exception: " << e.what() << "\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << "Other exception: " << e.what() << "\n";
    }
    catch (...)
    {
        std::cerr << "Unknown exception\n";
    }
}

int main(int argc, char* argv[])
{
    try
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

        cfg::config_t config(args->config);

    #ifndef NDEBUG
        log::stream() << *args << "\n";
        log::stream() << *dbg_info << "\n";
        log::stream() << config << std::endl;
    #endif

        int errnum;
        pid_t child_pid = ptrace_wrapper::instance.fork(errnum, &run_target, args->argv);
        if (child_pid > 0)
        {
            profiler prof(child_pid, (*args).profiler_flags,
                std::move(*dbg_info),
                std::move(config));
            if (!args->same_target())
            {
                if (auto err = prof.await_executable(args->target))
                {
                    std::cerr << err << std::endl;
                    return 1;
                }
            }

            auto results = prof.run();
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
    catch (...)
    {
        handle_exception();
        return 1;
    }
}
