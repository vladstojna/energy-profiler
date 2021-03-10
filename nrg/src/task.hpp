// task.hpp

#pragma once

#include "execution.hpp"
#include "holder.hpp"

namespace nrgprf
{
    using task = holder<execution>;

    template<>
    template<>
    size_t nrgprf::task::add();
}
