#pragma once

#include "../../visibility.hpp"
#include "gpu_handle.hpp"

#include <nrg/readings_type.hpp>
#include <nrg/types.hpp>

#include <array>
#include <iosfwd>
#include <vector>

namespace nrgprf
{
    class sample;

    struct NRG_LOCAL lib_handle
    {
        lib_handle();
        ~lib_handle();

        lib_handle(const lib_handle&);
        lib_handle(lib_handle&&);
        lib_handle& operator=(const lib_handle&);
        lib_handle& operator=(lib_handle&&);
    };

    struct NRG_LOCAL reader_gpu_impl
    {
    private:
        static bool read_energy(sample&, size_t, gpu_handle, std::error_code&) noexcept;
        static bool read_power(sample&, size_t, gpu_handle, std::error_code&) noexcept;

    public:
        struct NRG_LOCAL event
        {
            gpu_handle handle;
            size_t stride;
            decltype(&read_energy) read_func;
        };

        static result<readings_type::type> support(device_mask);

        lib_handle handle;
        std::array<std::array<int8_t, 2>, max_devices> event_map;
        std::vector<event> events;

        reader_gpu_impl(readings_type::type, device_mask, std::ostream&);

        bool read(sample&, uint8_t, std::error_code&) const noexcept;
        bool read(sample&, std::error_code&) const noexcept;

        int8_t event_idx(readings_type::type, uint8_t) const noexcept;
        size_t num_events() const noexcept;

        result<units_power> get_board_power(const sample&, uint8_t) const noexcept;
        result<units_energy> get_board_energy(const sample&, uint8_t) const noexcept;

    private:
        static result<readings_type::type> support(gpu_handle) noexcept;

        template<
            readings_type::type rt,
            typename UnitsRead,
            typename ToUnits,
            typename S
        > result<ToUnits> get_value(const S&, uint8_t) const noexcept;

        static constexpr std::array<std::pair<
            readings_type::type, decltype(event::read_func)>, 2> type_array =
        { {
            { readings_type::power, read_power },
            { readings_type::energy, read_energy }
        } };
    };
}
