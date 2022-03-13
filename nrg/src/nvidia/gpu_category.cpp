#include "../common/gpu/gpu_category.hpp"

#include <nvml.h>

namespace nrgprf
{
    std::string gpu_category_t::message(int ev) const
    {
        return nvmlErrorString(static_cast<nvmlReturn_t>(ev));
    }
}
