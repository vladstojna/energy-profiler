// types.hpp

#pragma once

#include <nrg/constants.hpp>
#include <nrg/error.hpp>
#include <nrg/units.hpp>
#include <util/expected.hpp>

#include <bitset>

namespace nrgprf
{
    using units_energy = microjoules<uintmax_t>;
    using units_power = microwatts<uintmax_t>;
    using units_time = std::chrono::time_point<std::chrono::high_resolution_clock>;

    template<typename R>
    using result = cmmn::expected<R, error>;

    using location_mask = std::bitset<max_locations>;
    using socket_mask = std::bitset<max_sockets>;
    using device_mask = std::bitset<max_devices>;
}
