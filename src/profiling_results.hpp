// profiling_results.hpp

#pragma once

#include "sampler.hpp"

#include <nrg/nrg.hpp>

#include <vector>

namespace tep
{

    class position_interface;
    class position_interval;

    class pos_execs
    {
    private:
        std::shared_ptr<position_interval> _xinterval;
        std::vector<timed_execution> _execs;

    public:
        pos_execs(std::shared_ptr<position_interval>&&);
        pos_execs(const std::shared_ptr<position_interval>&);

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

        friend std::ostream& operator<<(std::ostream&, const results_interface&);

    protected:
        virtual void print(std::ostream&) const = 0;
    };

    class result_execs : public results_interface
    {
    private:
        timed_execution _idle;
        std::vector<pos_execs> _execs;

    protected:
        result_execs(timed_execution&& idle);
        result_execs(const timed_execution& idle);

    public:
        void push_back(pos_execs&&);
        void push_back(const pos_execs&);

    protected:
        const std::vector<pos_execs>& positional_execs() const;
        const timed_execution& idle() const;
    };

    template<typename Reader>
    class result_execs_dev : public result_execs
    {
    private:
        Reader _reader;

    public:
        result_execs_dev(const Reader& r, timed_execution&& idle);
        result_execs_dev(const Reader& r, const timed_execution& idle);

    protected:
        void print(std::ostream&) const override;
    };


    class profiling_results
    {
    private:
        std::vector<std::unique_ptr<results_interface>> _results;

    public:
        profiling_results() = default;
        void push_back(std::unique_ptr<results_interface>&& results);

        friend std::ostream& operator<<(std::ostream&, const profiling_results&);
    };


    std::ostream& operator<<(std::ostream&, const profiling_results&);
    std::ostream& operator<<(std::ostream&, const results_interface&);

};
