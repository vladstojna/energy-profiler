#pragma once

#include "visibility.hpp"

#include <system_error>

namespace nrgprf
{
    struct NRG_LOCAL gpu_category_t : std::error_category
    {
        const char* name() const noexcept override;
        std::string message(int) const override;
        std::error_condition default_error_condition(int) const noexcept override;
    };
}
