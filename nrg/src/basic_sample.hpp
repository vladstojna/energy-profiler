// basic_sample.hpp

#pragma once

#include <chrono>
#include <utility>

namespace nrgprf
{

    using timepoint_t = std::chrono::time_point<std::chrono::high_resolution_clock>;
    using duration_t = std::chrono::nanoseconds;

    // return the timepoint that represents the time now
    inline timepoint_t now()
    {
        return std::chrono::high_resolution_clock::now();
    }

    class basic_sample
    {
    private:
        timepoint_t _timepoint;

    protected:
        basic_sample(const timepoint_t& tp);
        basic_sample(timepoint_t&& tp);

    public:
        const timepoint_t& timepoint() const { return _timepoint; }
        void timepoint(const timepoint_t& tp) { _timepoint = tp; }
        void timepoint(timepoint_t&& tp) { _timepoint = std::move(tp); }
    };

    duration_t operator-(const basic_sample& lhs, const basic_sample& rhs);

}
