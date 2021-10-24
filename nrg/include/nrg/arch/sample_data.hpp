#pragma once

#include <nrg/arch.hpp>
#include <nrg/constants.hpp>

#include <array>
#include <cstdint>

namespace nrgprf
{
    namespace detail
    {
    #if defined NRG_X86_64
        struct sample_data
        {
            std::array<uint64_t, max_cpu_events> cpu;
            std::array<uint32_t, max_devices> gpu_power;
            std::array<uint64_t, max_devices> gpu_energy;
        };
    #elif defined NRG_PPC64
        struct sample_data
        {
            std::array<uint64_t, max_cpu_events> timestamps;
            std::array<uint16_t, max_cpu_events> cpu;
            std::array<uint32_t, max_devices> gpu_power;
            std::array<uint64_t, max_devices> gpu_energy;
        };
    #endif
    }
}
