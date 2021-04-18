// profiler.hpp

#pragma once

#include "config.hpp"
#include "dbg.hpp"
#include "trap.hpp"

namespace tep
{

    struct profiling_results;

    class profiler
    {
    private:
        pid_t _child;
        bool _pie;
        dbg_line_info _dli;
        config_data _cd;
        trap_set _traps;

    public:
        profiler(pid_t child, bool pie, const dbg_line_info& dli, const config_data& cd);
        profiler(pid_t child, bool pie, const dbg_line_info& dli, config_data&& cd);
        profiler(pid_t child, bool pie, dbg_line_info&& dli, const config_data& cd);
        profiler(pid_t child, bool pie, dbg_line_info&& dli, config_data&& cd);

        const dbg_line_info& debug_line_info() const;
        const config_data& config() const;
        const trap_set& traps() const;

        cmmn::expected<profiling_results, tracer_error> run();
    };

}
