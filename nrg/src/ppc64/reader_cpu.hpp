#pragma once

#include "../visibility.hpp"

#include <nrg/types.hpp>

#include <array>
#include <iosfwd>
#include <memory>
#include <vector>

namespace nrgprf
{
    class error;
    class sample;

    enum class NRG_LOCAL sensor_type : uint16_t;
    enum class NRG_LOCAL sensor_loc : uint16_t;
    struct NRG_LOCAL sensor_buffers;

    struct NRG_LOCAL sensor_names_entry
    {
        constexpr static size_t size = 48;
        constexpr static size_t name_sz = 16;
        constexpr static size_t unit_sz = 4;
        char name[name_sz];
        char units[unit_sz];
        uint16_t gsid;
        double freq;
        double scaling_factor;
        sensor_type type;
        sensor_loc location;
        uint8_t structure_version;
        uint32_t reading_offset;
        uint8_t specific_info1;
    };

    struct NRG_LOCAL event_data
    {
        uint32_t occ_num;
        std::vector<sensor_names_entry> entries;
    };

    struct NRG_LOCAL reader_impl
    {
        // the file here functions as a cache, so as to avoid opening the file every time
        // we want to read the sensors
        std::shared_ptr<std::ifstream> _file;
        std::array<std::array<int8_t, max_domains>, max_sockets> _event_map;
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
            const std::vector<sensor_names_entry>& entries,
            uint32_t occ_num,
            uint32_t location,
            std::ostream& os);

        error read_single_occ(
            const event_data& ed,
            sensor_buffers& sbuffs,
            sample& s) const;
    };
}
