#pragma once

#include <nrg/arch.hpp>
#include <nrg/units.hpp>

namespace nrgprf
{
    namespace detail
    {
    #if defined NRG_X86_64
        using reader_return = microjoules<uintmax_t>;
    #elif defined NRG_PPC64
        struct reader_return_st
        {
            std::chrono::time_point<std::chrono::high_resolution_clock> timestamp;
            microwatts<uintmax_t> power;
        };
        using reader_return = reader_return_st;
    #endif
    }
}
