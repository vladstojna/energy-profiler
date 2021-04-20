// profiling_results.hpp

#pragma once

#include "config.hpp"
#include "reader_container.hpp"

#include <nrg.hpp>

#include <vector>

namespace tep
{

    struct idle_results
    {
        nrgprf::execution cpu_readings;
        nrgprf::execution gpu_readings;

        idle_results();
        idle_results(nrgprf::execution&& cpur, nrgprf::execution&& gpur);
    };

    struct section_results
    {
        config_data::section section;
        nrgprf::task readings;

        section_results(const config_data::section& sec);
    };

    struct profiling_results
    {
        reader_container readers;
        std::vector<section_results> results;

        profiling_results(reader_container&& rc);
        void add_execution(const config_data::section& sec, nrgprf::execution&& exec);
    };


    std::ostream& operator<<(std::ostream& os, const profiling_results& pr);

    bool operator==(const section_results& lhs, const section_results& rhs);
    bool operator==(const section_results& lhs, const config_data::section& rhs);
    bool operator==(const config_data::section& lhs, const section_results& rhs);

};
