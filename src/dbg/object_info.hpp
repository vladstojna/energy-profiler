#pragma once

#include <memory>
#include <string_view>
#include <vector>

#include "elf.hpp"
#include "dwarf.hpp"

namespace tep::dbg
{
    struct object_info
    {
        explicit object_info(std::string_view);

        const executable_header& header() const noexcept;
        const std::vector<function_symbol>& function_symbols() const noexcept;
        const std::vector<compilation_unit>& compilation_units() const noexcept;

    private:
        struct impl;
        std::shared_ptr<const impl> impl_;
    };

    std::ostream& operator<<(std::ostream&, const object_info&);
} // namespace tep::dbg
