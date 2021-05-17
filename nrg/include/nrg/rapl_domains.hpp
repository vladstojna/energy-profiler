// rapl_domains.hpp

#pragma once

#include <array>
#include <cinttypes>
#include <type_traits>
#include <iosfwd>

namespace nrgprf
{

    // define the domain mask
    enum class rapl_domain : uint8_t
    {
        NONE = 0,
        PKG = 1 << 0, // package
        PP0 = 1 << 1, // power-plane 0 i.e. cores
        PP1 = 1 << 2, // power-plane 1 i.e. uncore
        DRAM = 1 << 3 // DRAM estimation + memory controller
    };

    // operators

    inline rapl_domain operator&(rapl_domain lhs, rapl_domain rhs)
    {
        return static_cast<rapl_domain>(
            static_cast<std::underlying_type_t<rapl_domain>>(lhs) &
            static_cast<std::underlying_type_t<rapl_domain>>(rhs));
    }

    inline rapl_domain operator|(rapl_domain lhs, rapl_domain rhs)
    {
        return static_cast<rapl_domain>(
            static_cast<std::underlying_type_t<rapl_domain>>(lhs) |
            static_cast<std::underlying_type_t<rapl_domain>>(rhs));
    }

    inline rapl_domain operator&=(rapl_domain& lhs, rapl_domain rhs)
    {
        lhs = static_cast<rapl_domain>(
            static_cast<std::underlying_type_t<rapl_domain>>(lhs) &
            static_cast<std::underlying_type_t<rapl_domain>>(rhs));
        return lhs;
    }

    inline rapl_domain operator|=(rapl_domain& lhs, rapl_domain rhs)
    {
        lhs = static_cast<rapl_domain>(
            static_cast<std::underlying_type_t<rapl_domain>>(lhs) |
            static_cast<std::underlying_type_t<rapl_domain>>(rhs));
        return lhs;
    }

    std::ostream& operator<<(std::ostream& os, const rapl_domain& rd);

}
