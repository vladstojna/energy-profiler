// constants.hpp

#pragma once

#include <nrg/arch.hpp>
#include <nrg/arch/constants.hpp>

#include <cstdint>

namespace nrgprf {
namespace locmask {
constexpr uint32_t pkg = 0x01;
constexpr uint32_t cores = 0x02;
constexpr uint32_t uncore = 0x04;
constexpr uint32_t mem = 0x08;
constexpr uint32_t sys = 0x10;
constexpr uint32_t gpu = 0x20;
constexpr uint32_t all = pkg | cores | uncore | mem | sys | gpu;
} // namespace locmask

constexpr size_t max_locations = 32;
constexpr size_t max_sockets = 8;
constexpr size_t max_devices = 8;

constexpr size_t max_domains = detail::max_domains;
constexpr size_t max_cpu_events = max_sockets * max_domains;
constexpr size_t max_gpu_events = max_devices;
} // namespace nrgprf
