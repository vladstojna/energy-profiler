// error_codes.hpp

#pragma once

#include <iostream>

namespace nrgprf
{

    enum class error_code
    {
        SUCCESS = 0,
        NOT_IMPL,
        SYSTEM,
        READ_ERROR,
        SETUP_ERROR,
        NO_EVENT,
        OUT_OF_BOUNDS,
        UNKNOWN_ERROR,
        BAD_ALLOC
    };

    inline std::ostream& operator<<(std::ostream& os, const error_code& other)
    {
        return os << static_cast<std::underlying_type_t<error_code>>(other);
    }

}
