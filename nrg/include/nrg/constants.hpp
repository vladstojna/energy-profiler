// constants.hpp

#pragma once

#include <nrg/arch.hpp>

#include <cstddef>

namespace nrgprf
{
#if defined(NRG_X86_64)
    // RAPL has up to 4 domains/locations (Skylake has 5 but we ignore PSys for now)
    constexpr const size_t rapl_domains = 4;
    constexpr const size_t max_domains = rapl_domains;
#elif defined(NRG_PPC64)
    // The On Chip Controller (OCC) in Power9 systems supports the following locations:
    // system, gpu, processor, memory.
    // The processor location containes up to 3 sensors: package, vdd (cores) and vdn (nest)
    // Unlike RAPL, there is no uncore sensor but there is an associated timestamp
    constexpr const size_t occ_domains = 6;
    constexpr const size_t max_domains = occ_domains;
#endif

    constexpr const size_t max_locations = 32;
    constexpr const size_t max_sockets = 8;
    constexpr const size_t max_devices = 8;

    constexpr const size_t max_cpu_events = max_sockets * max_domains;
    constexpr const size_t max_gpu_events = max_devices;
}
