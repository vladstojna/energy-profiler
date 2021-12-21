#pragma once

#include "../visibility.hpp"

#include <nrg/readings_type.hpp>
#include <nrg/types.hpp>

namespace nrgprf
{
    class error;
    class sample;

    struct NRG_LOCAL reader_gpu_impl
    {
        static result<readings_type::type> support(device_mask);

        reader_gpu_impl(readings_type::type, device_mask, error&, std::ostream&);

        error read(sample&) const;
        error read(sample&, uint8_t) const;

        size_t num_events() const;
        int8_t event_idx(readings_type::type, uint8_t) const;

        result<units_power> get_board_power(const sample&, uint8_t) const;
        result<units_energy> get_board_energy(const sample&, uint8_t) const;
    };
}
