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
        struct output_mapping
        {
            using distance_pair = std::pair<
                std::iterator_traits<profiling_results::container::iterator>::difference_type,
                std::iterator_traits<group_output::container::iterator>::difference_type
            >;
            using map_type = std::unordered_map<addr_bounds, distance_pair, addr_bounds_hash>;

            map_type map;
            profiling_results results;

            output_mapping() = default;

            bool insert(addr_bounds bounds,
                const reader_container& readers,
                const idle_results& idle,
                const config_data::section_group& group,
                const config_data::section& sec);

            section_output* find(addr_bounds bounds);
        };

        pid_t _tid;
        pid_t _child;
        flags _flags;
        dbg_info _dli;
        config_data _cd;
        reader_container _readers;
        registered_traps _traps;
        idle_results _idle;
        output_mapping _output;

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
            const config_data::section_group& group,
            const config_data::section& sec,
            const config_data::function& func,
            uintptr_t entrypoint);

        cmmn::expected<start_addr, tracer_error> insert_traps_position_start(
            const config_data::section& sec,
            const config_data::position& pos,
            uintptr_t entrypoint);

        tracer_error insert_traps_position_end(
            const config_data::section_group& group,
            const config_data::section& sec,
            const config_data::position& pos,
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
