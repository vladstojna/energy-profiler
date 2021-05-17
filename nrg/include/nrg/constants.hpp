// constants.hpp

#pragma once

#include <cstddef>

namespace nrgprf
{
    // RAPL has up to 4 domains (Skylake has 5 but we ignore PSys for now)
    constexpr size_t rapl_domains = 4;
    constexpr size_t max_sockets = 8;
    constexpr size_t max_devices = 8;

    constexpr size_t max_cpu_events = max_sockets * rapl_domains;
    constexpr size_t max_gpu_events = max_devices;
}
