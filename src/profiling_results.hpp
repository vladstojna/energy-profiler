// profiling_results.hpp

#pragma once

#include "config.hpp"
#include "reader_container.hpp"
#include "sampler.hpp"

#include <nrg/nrg.hpp>

#include <vector>

namespace tep
{

    struct idle_results
    {
        timed_execution cpu_readings;
        timed_execution gpu_readings;

        idle_results();
        idle_results(timed_execution&& cpur, timed_execution&& gpur);
    };

    struct section_results
    {
        config_data::section section;
        std::vector<timed_execution> readings;

        section_results(const config_data::section& sec);
    };

    struct profiling_results
    {
        reader_container readers;
        std::vector<section_results> results;
        idle_results idle_res;

        profiling_results(reader_container&& rc, idle_results&& ir);
        void add_execution(const config_data::section& sec, timed_execution&& exec);
    };


    std::ostream& operator<<(std::ostream& os, const profiling_results& pr);

    bool operator==(const section_results& lhs, const section_results& rhs);
    bool operator==(const section_results& lhs, const config_data::section& rhs);
    bool operator==(const config_data::section& lhs, const section_results& rhs);

};
