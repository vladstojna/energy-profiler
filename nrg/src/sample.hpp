// sample.hpp

#pragma once

#include "basic_sample.hpp"
#include "rapl_domains.hpp"

namespace nrgprf
{

    class sample : public basic_sample
    {
    private:
        long long _values[MAX_SOCKETS * MAX_RAPL_DOMAINS + MAX_SOCKETS];

    public:
        sample(const timepoint_t& tp) : basic_sample(tp) {}
        sample(timepoint_t&& tp) : basic_sample(std::move(tp)) {}

        const long long* values() const { return _values; }
        long long* values() { return _values; }

        long long get(size_t idx) const { return _values[idx]; }
    };

}
