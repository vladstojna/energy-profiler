#pragma once

#include "../visibility.hpp"

#include <nrg/types.hpp>

#include <iosfwd>
#include <vector>

namespace nrgprf
{
    class error;
    class sample;

    struct NRG_LOCAL file_descriptor
    {
        static result<file_descriptor> create(const char* file);

        int value;

        file_descriptor(const char* file, error& err);
        ~file_descriptor() noexcept;

        file_descriptor(const file_descriptor& fd);
        file_descriptor(file_descriptor&& fd) noexcept;
        file_descriptor& operator=(file_descriptor&& other) noexcept;
    };

    struct NRG_LOCAL event_data
    {
        file_descriptor fd;
        mutable uint64_t max;
        mutable uint64_t prev;
        mutable uint64_t curr_max;
        event_data(file_descriptor&& fd, uint64_t max);
    };

    struct NRG_LOCAL reader_impl
    {
        std::array<std::array<int32_t, max_domains>, max_sockets> _event_map;
        std::vector<event_data> _active_events;

        reader_impl(location_mask, socket_mask, error&, std::ostream&);

        error read(sample&) const;
        error read(sample&, uint8_t) const;
        size_t num_events() const;

        template<typename Location>
        int32_t event_idx(uint8_t) const;

        template<typename Location>
        result<sensor_value> value(const sample&, uint8_t) const;

    private:
        error add_event(
            const char* base,
            location_mask dmask,
            uint8_t skt,
            std::ostream& os);
    };
}
