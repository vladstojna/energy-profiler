#pragma once

#include "../visibility.hpp"

#include <nrg/types.hpp>

namespace nrgprf
{
    class error;
    class sample;

    struct NRG_LOCAL reader_impl
    {
        reader_impl(location_mask, socket_mask, error&, std::ostream&);

        error read(sample&) const;
        error read(sample&, uint8_t) const;
        size_t num_events() const;

        template<typename Location>
        int32_t event_idx(uint8_t) const;

        template<typename Location>
        result<sensor_value> value(const sample&, uint8_t) const;
    };
}
