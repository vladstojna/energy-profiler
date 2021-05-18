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

    template<typename R>
    using result = cmmn::expected<R, error>;

    using rapl_mask = std::bitset<rapl_domains>;
    using socket_mask = std::bitset<max_sockets>;
    using device_mask = std::bitset<max_devices>;
}
