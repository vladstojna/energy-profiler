#pragma once

#include <cstdint>

namespace nrgprf
{
    namespace readings_type
    {
        enum type : std::uint32_t
        {
            power = 1 << 0,
            energy = 1 << 1
        };

        type operator|(type lhs, type rhs);
        type operator&(type lhs, type rhs);
        type operator^(type lhs, type rhs);

        extern const type all;
    }
}
