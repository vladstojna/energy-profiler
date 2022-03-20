#pragma once

#include <string>
#include <string_view>
#include <optional>
#include <system_error>

namespace tep
{
    namespace dbg
    {
        /**
         * @brief demangle symbol name
         *
         * @param mangled mangled name
         * @param ec error code output argument
         * @param demangle_types whether to demangle mangled type names
         * @return std::optional<std::string>
         */
        std::optional<std::string> demangle(
            std::string_view mangled,
            std::error_code& ec,
            bool demangle_types = false);
    }
}
