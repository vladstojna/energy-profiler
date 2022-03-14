#pragma once

#include <nrg/sample.hpp>

#include <chrono>

namespace tep
{
    struct timed_sample
    {
    #ifdef TEP_USE_SYSTEM_CLOCK
        using clock = std::chrono::system_clock;
    #else
        using clock = std::chrono::steady_clock;
    #endif
        using time_point = std::chrono::time_point<clock>;
        using duration = std::chrono::nanoseconds;

        time_point timestamp;
        nrgprf::sample sample;

        operator nrgprf::sample& () noexcept;
        operator const nrgprf::sample& () const noexcept;

        bool operator==(const timed_sample&) const noexcept;
        bool operator!=(const timed_sample&) const noexcept;

        duration operator-(const timed_sample&) const noexcept;
    };
}
