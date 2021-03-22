// profiler.h

#pragma once

#include <unordered_map>
#include <vector>

#include "config.hpp"
#include "dbg.hpp"
#include "tracer.hpp"

namespace tep
{
    struct section_results
    {
        config_data::section section;
        std::vector<fallible_execution> executions;

        section_results(config_data::section&& sec, std::vector<fallible_execution>&& execs);
    };

    struct profiling_results
    {
        nrgprf::reader_rapl rdr_cpu;
        nrgprf::reader_gpu rdr_gpu;
        std::vector<section_results> results;

        profiling_results(nrgprf::reader_rapl&& rr, nrgprf::reader_gpu&& rg);
    };

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

        tracer_expected<profiling_results> run();
    };

}
