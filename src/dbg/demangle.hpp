#pragma once

#include <string>
#include <string_view>
#include <optional>
#include <system_error>

namespace dbg
{
    std::optional<std::string> demangle(
        std::string_view,
        std::error_code&,
        bool demangle_types = false);
}
