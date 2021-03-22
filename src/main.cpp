// main.cpp

#include <cstring>
#include <fstream>
#include <iostream>
#include <unordered_set>

#include "cmdargs.h"
#include "dbg.hpp"
#include "profiler.h"
#include "ptrace_wrapper.hpp"
#include "target.h"
#include "util.h"

int main(int argc, char* argv[])
{
    tep::arguments args;
    int idx = tep::parse_arguments(argc, argv, args);
    if (idx < 0)
        return 1;
    std::cout << args << "\n";

    int error;
    pid_t child_pid = tep::ptrace_wrapper::instance.fork(error, &tep::run_target, &argv[idx]);
    if (child_pid > 0)
    {
        tep::dbg_expected<tep::dbg_line_info> dbg_info = tep::dbg_line_info::create(argv[idx]);
        if (!dbg_info)
        {
            std::cerr << dbg_info.error() << std::endl;
            return 1;
        }

        tep::cfg_result config = tep::load_config("../random/random.xml");
        if (!config)
        {
            std::cerr << config.error() << std::endl;
            return 1;
        }

        std::cout << dbg_info.value() << std::endl;
        std::cout << config.value() << std::endl;

        tep::profiler profiler(child_pid, std::move(dbg_info.value()), std::move(config.value()));
        tep::tracer_error error = profiler.run();
        if (error)
        {
            std::cerr << error << std::endl;
            return 1;
        }
        return 0;
    }
    else if (child_pid == -1)
    {
        tep::log(tep::log_lvl::error, "fork(): %s", strerror(error));
    }
    return 1;
}
