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

    namespace occ
    {
        constexpr size_t sensor_buffer_gap = 4096; // 4 kB
        constexpr size_t sensor_readings_size = 40 * 1024; // 40 kB
        constexpr size_t sensor_ping_buffer_offset = 0xdc00;
        constexpr size_t sensor_ping_buffer_size = sensor_readings_size;
        constexpr size_t sensor_pong_buffer_offset = 0x18c00;
        constexpr size_t sensor_pong_buffer_size = sensor_ping_buffer_size;

        enum class NRG_LOCAL sensor_type : uint16_t
        {
            generic = 0x0001,
            current = 0x0002,
            voltage = 0x0004,
            temp = 0x0008,
            util = 0x0010,
            time = 0x0020,
            freq = 0x0040,
            power = 0x0080,
            perf = 0x0200
        };

        enum class NRG_LOCAL sensor_loc : uint16_t
        {
            system = 0x0001,
            proc = 0x0002,
            partition = 0x0004,
            memory = 0x0008,
            vrm = 0x0010,
            occ = 0x0020,
            core = 0x0040,
            gpu = 0x0080,
            quad = 0x0100
        };

        struct NRG_LOCAL sensor_readings_buffer
        {
            constexpr static size_t size = sensor_readings_size;
            constexpr static size_t pad = 8; // valid byte + 7 bytes reserved
            uint8_t valid;
            uint8_t __reserved[7];
            uint8_t readings[size - pad];
        } __attribute__((__packed__));

        struct NRG_LOCAL sensor_buffers
        {
            constexpr static size_t size = sensor_pong_buffer_offset -
                sensor_ping_buffer_offset + sensor_pong_buffer_size;

            static_assert(size ==
                sensor_ping_buffer_size + sensor_pong_buffer_size + sensor_buffer_gap);

            sensor_readings_buffer ping;
            uint8_t __reserved[sensor_buffer_gap];
            sensor_readings_buffer pong;
        } __attribute__((__packed__));

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
    }

    struct NRG_LOCAL event_data
    {
        uint32_t occ_num;
        std::vector<occ::sensor_names_entry> entries;
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
            const std::vector<occ::sensor_names_entry>& entries,
            uint32_t occ_num,
            uint32_t location,
            std::ostream& os);

        error read_single_occ(
            const event_data& ed,
            occ::sensor_buffers& sbuffs,
            sample& s) const;
    };
}
