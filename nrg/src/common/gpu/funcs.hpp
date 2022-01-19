#pragma once

#include "../../visibility.hpp"

#include <nrg/readings_type.hpp>
#include <nrg/types.hpp>

#include <iosfwd>

namespace nrgprf
{
    NRG_LOCAL constexpr size_t bitpos(readings_type::type rt)
    {
        auto val = static_cast<std::underlying_type_t<readings_type::type>>(rt);
        size_t pos = 0;
        while ((val >>= 1) & 0x1)
            pos++;
        return pos;
    }

    NRG_LOCAL std::string event_added(unsigned int, readings_type::type);
    NRG_LOCAL std::string event_not_added(unsigned int, readings_type::type);
    NRG_LOCAL std::string event_not_supported(unsigned int, readings_type::type);
    NRG_LOCAL error assert_device_count(unsigned int);
}
