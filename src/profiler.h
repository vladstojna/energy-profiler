// profiler.h

#pragma once

#include <unordered_map>

#include "config.hpp"
#include "dbg.hpp"
#include "tracer.hpp"

namespace tep
{

    class profiler
    {
    private:
        pid_t _child;
        dbg_line_info _dli;
        config_data _cd;
        std::unordered_map<uintptr_t, trap_data> _traps;

    public:
        profiler(pid_t child, const dbg_line_info& dli, const config_data& cd);
        profiler(pid_t child, const dbg_line_info& dli, config_data&& cd);
        profiler(pid_t child, dbg_line_info&& dli, const config_data& cd);
        profiler(pid_t child, dbg_line_info&& dli, config_data&& cd);

        tracer_error run();
    };

}
