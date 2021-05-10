// task.hpp

#pragma once

#include "execution.hpp"
#include "holder.hpp"

namespace nrgprf
{
    using task = holder<execution>;

    template<>
    template<>
    execution& nrgprf::task::add();
}
