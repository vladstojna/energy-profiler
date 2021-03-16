// sample.hpp

#pragma once

#include "basic_sample.hpp"
#include "rapl_domains.hpp"

namespace nrgprf
{

    class sample : public basic_sample
    {
    private:
        uint64_t _values[MAX_SOCKETS * MAX_RAPL_DOMAINS + MAX_SOCKETS];

    public:
        sample(const timepoint_t& tp) : basic_sample(tp) {}
        sample(timepoint_t&& tp) : basic_sample(std::move(tp)) {}

        uint64_t get(size_t idx) const { return _values[idx]; }
        void set(size_t idx, uint64_t val) { _values[idx] = val; }
    };

}
