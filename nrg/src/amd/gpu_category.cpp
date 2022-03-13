#include "../gpu_category.hpp"

#include <rocm_smi/rocm_smi.h>

namespace nrgprf
{
    std::string gpu_category_t::message(int ev) const
    {
        const char* str = nullptr;
        auto status = static_cast<rsmi_status_t>(ev);
        if (RSMI_STATUS_SUCCESS != rsmi_status_string(status, &str))
            return "(unrecognized error code)";
        return str;
    }
}
