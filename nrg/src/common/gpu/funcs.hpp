#pragma once

#include "../../visibility.hpp"

#include <nrg/readings_type.hpp>
#include <nrg/types.hpp>

#include <iosfwd>
#include <sstream>

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

    template<typename T>
    NRG_LOCAL std::string to_string(const T& item)
    {
        std::ostringstream os;
        os << item;
        return os.str();
    }

    NRG_LOCAL std::ostream& operator<<(std::ostream&, readings_type::type);
    NRG_LOCAL std::string event_added(unsigned int, readings_type::type);
    NRG_LOCAL std::string event_not_added(unsigned int, readings_type::type);
    NRG_LOCAL std::string event_not_supported(unsigned int, readings_type::type);
}
