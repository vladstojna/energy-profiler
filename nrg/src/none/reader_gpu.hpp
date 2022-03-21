#pragma once

#include "../visibility.hpp"

#include <nrg/readings_type.hpp>
#include <nrg/types.hpp>

namespace nrgprf
{
    class sample;

    struct NRG_LOCAL reader_gpu_impl
    {
        static result<readings_type::type> support(device_mask) noexcept;

        reader_gpu_impl(readings_type::type, device_mask, std::ostream&);

        bool read(sample&, std::error_code&) const noexcept;
        bool read(sample&, uint8_t, std::error_code&) const noexcept;

        size_t num_events() const noexcept;
        int8_t event_idx(readings_type::type, uint8_t) const noexcept;

        result<units_power> get_board_power(const sample&, uint8_t) const noexcept;
        result<units_energy> get_board_energy(const sample&, uint8_t) const noexcept;
    };
}
