// profiling_results.hpp

#pragma once

#include "config.hpp"

#include <nrg.hpp>

#include <vector>

namespace tep
{

    struct section_results
    {
        config_data::section section;
        nrgprf::task readings;

        section_results(const config_data::section& sec);
    };

    struct profiling_results
    {
        nrgprf::reader_rapl rdr_cpu;
        nrgprf::reader_gpu rdr_gpu;
        std::vector<section_results> results;

        profiling_results(nrgprf::reader_rapl&& rr, nrgprf::reader_gpu&& rg);
        void add_execution(const config_data::section& sec, nrgprf::execution&& exec);
    };


    std::ostream& operator<<(std::ostream& os, const profiling_results& pr);

    bool operator==(const section_results& lhs, const section_results& rhs);
    bool operator==(const section_results& lhs, const config_data::section& rhs);
    bool operator==(const config_data::section& lhs, const section_results& rhs);

};
