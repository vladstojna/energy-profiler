// flags.hpp

#pragma once

#include <nrg/types.hpp>

#include <iosfwd>

namespace tep
{

    struct flags
    {
        bool obtain_idle;
        nrgprf::location_mask locations;
        nrgprf::socket_mask sockets;
        nrgprf::device_mask devices;
    };

    std::ostream& operator<<(std::ostream& os, const flags& f);

}
