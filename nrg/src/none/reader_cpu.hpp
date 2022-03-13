#pragma once

#include "../visibility.hpp"

#include <nrg/types.hpp>

namespace nrgprf
{
    class sample;

    struct NRG_LOCAL reader_impl
    {
        reader_impl(location_mask, socket_mask, std::ostream&);

        bool read(sample&, std::error_code&) const noexcept;
        bool read(sample&, uint8_t, std::error_code&) const noexcept;
        size_t num_events() const noexcept;

        template<typename Location>
        int32_t event_idx(uint8_t) const noexcept;

        template<typename Location>
        result<sensor_value> value(const sample&, uint8_t) const noexcept;
    };
}
