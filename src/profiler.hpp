// profiler.hpp

#pragma once

#include "config.hpp"
#include "dbg.hpp"
#include "flags.hpp"
#include "output.hpp"
#include "reader_container.hpp"
#include "trap.hpp"

#include <util/expectedfwd.hpp>

namespace tep
{
    class profiling_results;

    class profiler
    {
    public:
        template<typename D = dbg_info, typename C = cfg::config_t>
        static nonstd::expected<profiler, tracer_error>
            create(pid_t, const flags&, D&&, C&&);

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

            bool insert(addr_bounds,
                const reader_container&,
                const cfg::group_t&,
                const cfg::section_t&);

            section_output* find(addr_bounds);
        };

        pid_t _tid;
        pid_t _child;
        flags _flags;
        dbg_info _dli;
        cfg::config_t _cd;
        reader_container _readers;
        registered_traps _traps;
        output_mapping _output;

    public:
        profiler(pid_t child, const flags& flags,
            const dbg_info& dli, const cfg::config_t& cd, tracer_error& err);

        profiler(pid_t child, const flags& flags,
            const dbg_info& dli, cfg::config_t&& cd, tracer_error& err);

        profiler(pid_t child, const flags& flags,
            dbg_info&& dli, const cfg::config_t& cd, tracer_error& err);

        profiler(pid_t child, const flags& flags,
            dbg_info&& dli, cfg::config_t&& cd, tracer_error& err);

        const dbg_info& debug_line_info() const;
        const cfg::config_t& config() const;
        const registered_traps& traps() const;

        tracer_error await_executable(const std::string& name) const;
        nonstd::expected<profiling_results, tracer_error> run();

    private:
        tracer_error obtain_idle_results();

        tracer_error insert_traps_function(
            const cfg::group_t&,
            const cfg::section_t&,
            const cfg::function_t&,
            uintptr_t);

        tracer_error insert_traps_address_range(
            const cfg::group_t&,
            const cfg::section_t&,
            const cfg::address_range_t&,
            uintptr_t
        );

        nonstd::expected<start_addr, tracer_error> insert_traps_position_start(
            const cfg::section_t&,
            const cfg::position_t&,
            uintptr_t);

        tracer_error insert_traps_position_end(
            const cfg::group_t&,
            const cfg::section_t&,
            const cfg::position_t&,
            uintptr_t,
            start_addr);
    };
}
