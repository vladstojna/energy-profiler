// sample.hpp

#pragma once

#include <nrg/constants.hpp>
#include <nrg/arch/sample_data.hpp>

#include <array>
#include <cstdint>
#include <chrono>

namespace nrgprf
{
    class reader;
    class reader_rapl;
    class reader_gpu;

    class sample
    {
    public:
        using value_type = uint64_t;

        detail::sample_data data;

        sample();

        bool operator==(const sample& rhs) const;
        bool operator!=(const sample& rhs) const;

        explicit operator bool() const;
    };
}
