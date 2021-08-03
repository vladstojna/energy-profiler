// sample.hpp

#pragma once

#include <nrg/constants.hpp>
#include <nrg/arch/sample_data.hpp>

#include <array>
#include <cstdint>
#include <chrono>

namespace nrgprf
{

    class error;
    class reader;
    class reader_rapl;
    class reader_gpu;

    class sample
    {
    public:
        using value_type = uint64_t;

        friend reader_rapl;
        friend reader_gpu;

    private:
        detail::sample_data data;

    public:
        sample();
        sample(const reader& reader, error& e);

        bool operator==(const sample& rhs) const;
        bool operator!=(const sample& rhs) const;

        explicit operator bool() const;
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
        timed_sample();
        timed_sample(const reader&, error&);

        const time_point& timepoint() const;

        bool operator==(const timed_sample& rhs) const;
        bool operator!=(const timed_sample& rhs) const;

        duration operator-(const timed_sample& rhs) const;

        operator const sample& () const;
        operator sample& ();
    };

}
