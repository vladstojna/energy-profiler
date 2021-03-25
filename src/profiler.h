// profiler.h

#pragma once

#include <vector>

#include <nrg.hpp>

#include "config.hpp"
#include "dbg.hpp"
#include "trap.hpp"

namespace tep
{

    struct section_results
    {
        config_data::section section;
        nrgprf::task readings;

        section_results(const config_data::section& sec);
    };

    bool operator==(const section_results& lhs, const section_results& rhs);
    bool operator==(const section_results& lhs, const config_data::section& rhs);
    bool operator==(const config_data::section& lhs, const section_results& rhs);


    struct profiling_results
    {
        nrgprf::reader_rapl rdr_cpu;
        nrgprf::reader_gpu rdr_gpu;
        std::vector<section_results> results;

        profiling_results(nrgprf::reader_rapl&& rr, nrgprf::reader_gpu&& rg);
        void add_execution(const config_data::section& sec, nrgprf::execution&& exec);
    };


    class profiler
    {
    private:
        pid_t _child;
        dbg_line_info _dli;
        config_data _cd;
        trap_set _traps;

    public:
        profiler(pid_t child, const dbg_line_info& dli, const config_data& cd);
        profiler(pid_t child, const dbg_line_info& dli, config_data&& cd);
        profiler(pid_t child, dbg_line_info&& dli, const config_data& cd);
        profiler(pid_t child, dbg_line_info&& dli, config_data&& cd);

        const dbg_line_info& debug_line_info() const;
        const config_data& config() const;
        const trap_set& traps() const;

        cmmn::expected<profiling_results, tracer_error> run();
    };

}
