// sample.hpp

#pragma once

#include <nrg/constants.hpp>
#include <nrg/types.hpp>

#include <array>

namespace nrgprf
{

    class error;
    class reader;

    class sample
    {
    public:
        using value_type = units_energy::rep;

    private:
        std::array<value_type, max_cpu_events + max_gpu_events> _values;

    public:
        sample(const reader&, error&);

        value_type& at_cpu(size_t idx);
        value_type& at_gpu(size_t idx);

        result<value_type> at_cpu(size_t idx) const;
        result<value_type> at_gpu(size_t idx) const;

        bool operator==(const sample& rhs) const;
        bool operator!=(const sample& rhs) const;

        sample operator+(const sample& rhs) const;
        sample operator-(const sample& rhs) const;
        sample operator/(sample::value_type rhs) const;
        sample operator*(sample::value_type rhs) const;

        sample& operator+=(const sample& rhs);
        sample& operator-=(const sample& rhs);
        sample& operator*=(sample::value_type rhs);
        sample& operator/=(sample::value_type rhs);
    };


    class timed_sample
    {
    public:
        using time_point = std::chrono::time_point<std::chrono::high_resolution_clock>;
        using duration = std::chrono::nanoseconds;

    private:
        time_point _timepoint;
        sample _sample;

    public:
        timed_sample(const reader&, error&);

        sample& smp();
        const sample& smp() const;

        const time_point& timepoint() const;

        bool operator==(const timed_sample& rhs);
        bool operator!=(const timed_sample& rhs);

        duration operator-(const timed_sample& rhs);
    };

}
