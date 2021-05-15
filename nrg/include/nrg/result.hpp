// result.hpp

#pragma once

#include "error.hpp"

#include <util/expected.hpp>

namespace nrgprf
{
    template<typename R>
    using result = cmmn::expected<R, error>;
}
