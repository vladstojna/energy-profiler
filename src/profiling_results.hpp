// profiling_results.hpp

#pragma once

#include "sampler.hpp"

#include <nrg/nrg.hpp>

#include <vector>

namespace tep
{

    class position_interval;

    class pos_execs
    {
    private:
        std::unique_ptr<position_interval> _xinterval;
        std::vector<timed_execution> _execs;

    public:
        pos_execs(std::unique_ptr<position_interval>&&);

        void push_back(timed_execution&& exec);
        void push_back(const timed_execution& exec);

        const position_interval& interval() const;
        const std::vector<timed_execution>& execs() const;
    };

    struct idle_results
    {
        timed_execution cpu_readings;
        timed_execution gpu_readings;
    };


    class results_interface
    {
    public:
        virtual ~results_interface() = default;

        virtual void print(
            std::ostream& os,
            const position_interval& pos,
            const timed_execution& exec
        ) const = 0;
    };

    class results_holder : public results_interface
    {
    private:
        std::vector<std::unique_ptr<results_interface>> _results;

    public:
        results_holder() = default;

        void push_back(std::unique_ptr<results_interface>&& res);

        void print(
            std::ostream& os,
            const position_interval& pos,
            const timed_execution& exec
        ) const override;
    };

    template<typename Reader>
    class results_dev : public results_interface
    {
    private:
        Reader _reader;
        timed_execution _idle;

    public:
        results_dev(const Reader& r, const timed_execution& idle);
        results_dev(const Reader& r, timed_execution&& idle);

        void print(
            std::ostream& os,
            const position_interval& pos,
            const timed_execution& exec
        ) const override;
    };

    class profiling_results
    {
    public:
        using results_pair = std::pair<std::unique_ptr<results_interface>, pos_execs>;

    private:
        std::vector<results_pair> _results;

    public:
        profiling_results() = default;
        void push_back(results_pair&& results);

        friend std::ostream& operator<<(std::ostream&, const profiling_results&);
    };

    // operator overloads

    std::ostream& operator<<(std::ostream&, const profiling_results&);

    // deduction guides

    template<typename Reader>
    results_dev(const Reader& r)->results_dev<Reader>;

    // types

    using results_cpu = results_dev<nrgprf::reader_rapl>;
    using results_gpu = results_dev<nrgprf::reader_gpu>;

};
