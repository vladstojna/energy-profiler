// profiler.hpp

#pragma once

#include <nrg.hpp>

#include "config.hpp"
#include "dbg.hpp"
#include "flags.hpp"
#include "profiling_results.hpp"
#include "reader_container.hpp"
#include "trap.hpp"

namespace tep
{

    class profiler
    {
    public:
        template<typename D = dbg_line_info, typename C = config_data>
        static cmmn::expected<profiler, tracer_error> create(pid_t child, const flags& f,
            D&& dli, C&& cd);

    private:
        pid_t _tid;
        pid_t _child;
        flags _flags;
        dbg_line_info _dli;
        config_data _cd;
        reader_container _readers;
        trap_set _traps;
        idle_results _idle;

        profiler(pid_t child, const flags& flags,
            const dbg_line_info& dli, const config_data& cd, tracer_error& err);

        profiler(pid_t child, const flags& flags,
            const dbg_line_info& dli, config_data&& cd, tracer_error& err);

        profiler(pid_t child, const flags& flags,
            dbg_line_info&& dli, const config_data& cd, tracer_error& err);

        profiler(pid_t child, const flags& flags,
            dbg_line_info&& dli, config_data&& cd, tracer_error& err);

    public:
        const dbg_line_info& debug_line_info() const;
        const config_data& config() const;
        const trap_set& traps() const;

        cmmn::expected<profiling_results, tracer_error> run();

    private:
        tracer_error obtain_idle_results();
    };

    template<typename D, typename C>
    cmmn::expected<profiler, tracer_error> profiler::create(pid_t child, const flags& f,
        D&& dli, C&& cd)
    {
        tracer_error err = tracer_error::success();
        profiler prof(child, f, std::forward<D>(dli), std::forward<C>(cd), err);
        if (err)
            return err;
        return prof;
    }

}
