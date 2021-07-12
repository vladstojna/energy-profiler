// sample.hpp

#pragma once

#include <nrg/constants.hpp>

#include <array>
#include <cstdint>
#include <chrono>

namespace nrgprf
{

    class error;
    class reader;
    class reader_rapl;
    class reader_gpu;

#if defined(NRG_X86_64)

#define NRG_SAMPLE_DECLARE_MEMBERS \
    std::array<uint64_t, max_cpu_events> cpu; \
    std::array<uint32_t, max_gpu_events> gpu

#elif defined(NRG_PPC64)

#define NRG_SAMPLE_DECLARE_MEMBERS \
    std::array<uint64_t, max_cpu_events> timestamps; \
    std::array<uint16_t, max_cpu_events> cpu; \
    std::array<uint32_t, max_gpu_events> gpu

#endif // defined(NRG_X86_64)

    class sample
    {
    public:
        using value_type = uint64_t;

        friend reader_rapl;
        friend reader_gpu;

    private:
        NRG_SAMPLE_DECLARE_MEMBERS;

    public:
        sample();
        sample(const reader& reader, error& e);

        bool operator==(const sample& rhs) const;
        bool operator!=(const sample& rhs) const;

        sample operator+(const sample& rhs) const;
        sample operator-(const sample& rhs) const;
        sample operator/(value_type rhs) const;
        sample operator*(value_type rhs) const;

        sample& operator+=(const sample& rhs);
        sample& operator-=(const sample& rhs);
        sample& operator*=(value_type rhs);
        sample& operator/=(value_type rhs);

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
