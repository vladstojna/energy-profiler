#include "gpu_category.hpp"

#include <nrg/error.hpp>

namespace nrgprf
{
    const char* gpu_category_t::name() const noexcept
    {
        return "gpu";
    }

    std::error_condition gpu_category_t::default_error_condition(int) const noexcept
    {
        return nrgprf::error_cause::gpu_lib_error;
    }
}
