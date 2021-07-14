// constants.hpp

#pragma once

#include <nrg/arch.hpp>
#include <nrg/arch/constants.hpp>

namespace nrgprf
{
    constexpr const size_t max_locations = 32;
    constexpr const size_t max_sockets = 8;
    constexpr const size_t max_devices = 8;

    constexpr const size_t max_domains = detail::max_domains;
    constexpr const size_t max_cpu_events = max_sockets * max_domains;
    constexpr const size_t max_gpu_events = max_devices;
}
