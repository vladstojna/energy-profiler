// profiler.hpp

#pragma once

#include <nrg/nrg.hpp>

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
        template<typename D = dbg_info, typename C = config_data>
        static cmmn::expected<profiler, tracer_error> create(pid_t child, const flags& f,
            D&& dli, C&& cd);

    private:
        pid_t _tid;
        pid_t _child;
        flags _flags;
        dbg_info _dli;
        config_data _cd;
        reader_container _readers;
        registered_traps _traps;
        idle_results _idle;

        profiler(pid_t child, const flags& flags,
            const dbg_info& dli, const config_data& cd, tracer_error& err);

        profiler(pid_t child, const flags& flags,
            const dbg_info& dli, config_data&& cd, tracer_error& err);

        profiler(pid_t child, const flags& flags,
            dbg_info&& dli, const config_data& cd, tracer_error& err);

        profiler(pid_t child, const flags& flags,
            dbg_info&& dli, config_data&& cd, tracer_error& err);

    public:
        const dbg_info& debug_line_info() const;
        const config_data& config() const;
        const registered_traps& traps() const;

        cmmn::expected<profiling_results, tracer_error> run();

    private:
        tracer_error obtain_idle_results();

        tracer_error insert_traps_function(const config_data::section& s,
            const config_data::function& f, uintptr_t entrypoint);

        cmmn::expected<start_addr, tracer_error> insert_traps_position_start(
            const config_data::section& s,
            const config_data::position& p,
            uintptr_t entrypoint);

        tracer_error insert_traps_position_end(
            const config_data::position& p,
            uintptr_t entrypoint,
            start_addr start);
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
