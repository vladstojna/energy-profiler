#pragma once

#include <string>
#include <string_view>
#include <optional>
#include <system_error>

namespace tep
{
    namespace dbg
    {
        struct demangle_exception : std::system_error
        {
            using system_error::system_error;
        };

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

        /**
         * @brief demangle symbol name (with exception-throwing interface)
         *
         * @param mangled mangled name
         * @param demangle_types whether to demangle mangled type names
         * @return std::string
         */
        std::string demangle(
            std::string_view mangled,
            bool demangle_types = false);
    }
}
