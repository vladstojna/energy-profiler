// profiler.hpp

#pragma once

#include <nrg/nrg.hpp>

#include "config.hpp"
#include "dbg.hpp"
#include "flags.hpp"
#include "output.hpp"
#include "reader_container.hpp"
#include "trap.hpp"

namespace tep
{

    class profiling_results;

    class profiler
    {
    public:
        template<typename D = dbg_info, typename C = config_data>
        static cmmn::expected<profiler, tracer_error> create(pid_t child, const flags& f,
            D&& dli, C&& cd);

    private:
        using target_map = std::unordered_map<
            addr_bounds,
            config_data::section::target_cont,
            addr_bounds_hash>;

        pid_t _tid;
        pid_t _child;
        flags _flags;
        dbg_info _dli;
        config_data _cd;
        reader_container _readers;
        registered_traps _traps;
        idle_results _idle;
        target_map _targets;

    public:
        profiler(pid_t child, const flags& flags,
            const dbg_info& dli, const config_data& cd, tracer_error& err);

        profiler(pid_t child, const flags& flags,
            const dbg_info& dli, config_data&& cd, tracer_error& err);

        profiler(pid_t child, const flags& flags,
            dbg_info&& dli, const config_data& cd, tracer_error& err);

        profiler(pid_t child, const flags& flags,
            dbg_info&& dli, config_data&& cd, tracer_error& err);

        const dbg_info& debug_line_info() const;
        const config_data& config() const;
        const registered_traps& traps() const;

        cmmn::expected<profiling_results, tracer_error> run();

    private:
        tracer_error obtain_idle_results();

        tracer_error insert_traps_function(
            const config_data::section& s,
            const config_data::function& f,
            uintptr_t entrypoint);

        cmmn::expected<start_addr, tracer_error> insert_traps_position_start(
            const config_data::section& s,
            const config_data::position& p,
            uintptr_t entrypoint);

        tracer_error insert_traps_position_end(
            const config_data::section& s,
            const config_data::position& p,
            uintptr_t entrypoint,
            start_addr start);

        tracer_error insert_target(addr_bounds, const config_data::section::target_cont& tgts);
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
