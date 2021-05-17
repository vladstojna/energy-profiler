// types.hpp

#pragma once

#include <nrg/error.hpp>
#include <nrg/units.hpp>
#include <util/expected.hpp>

namespace nrgprf
{
    using units_energy = microjoules<uintmax_t>;
    using units_power = microwatts<uintmax_t>;

    template<typename R>
    using result = cmmn::expected<R, error>;
}
