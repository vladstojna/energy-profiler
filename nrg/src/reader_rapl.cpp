// reader_rapl.cpp

#include <nrg/arch.hpp>
#include <nrg/reader_rapl.hpp>

#include <cassert>
#include <iostream>

#ifndef CPU_NONE

#include <nrg/error.hpp>
#include <nrg/sample.hpp>
#include <util/concat.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <fstream>

#ifdef NRG_X86_64
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#endif // NRG_X86_64
#ifdef NRG_PPC64
#include <cmath>
#include <sstream>
#include <iomanip>
#endif

#endif // CPU_NONE

#include "util.hpp"

using namespace nrgprf;

#ifndef CPU_NONE

#if defined NRG_X86_64

namespace nrgprf::loc
{
    struct pkg : std::integral_constant<int, 0> {};
    struct cores : std::integral_constant<int, 1> {};
    struct uncore : std::integral_constant<int, 2> {};
    struct mem : std::integral_constant<int, 3> {};
    struct sys {};
    struct nest {};
    struct gpu {};
}

#elif defined NRG_PPC64

namespace nrgprf::loc
{
    struct pkg : std::integral_constant<int, 0> {};
    struct cores : std::integral_constant<int, 1> {};
    struct nest : std::integral_constant<int, 2> {};
    struct mem : std::integral_constant<int, 3> {};
    struct sys : std::integral_constant<int, 4> {};
    struct gpu : std::integral_constant<int, 5> {};
    struct uncore {};
}

#endif // defined NRG_X86_64

#endif // CPU_NONE

#ifndef CPU_NONE

static std::string system_error_str(std::string_view prefix)
{
    char buffer[256];
    return cmmn::concat(prefix, ": ", strerror_r(errno, buffer, sizeof(buffer)));
}

static result<uint8_t> count_sockets()
{
    char filename[128];
    bool pkg_found[max_sockets]{ false };
    uint8_t ret = 0;
    for (int i = 0; ; i++)
    {
        uint32_t pkg;
        snprintf(filename, sizeof(filename),
            "/sys/devices/system/cpu/cpu%d/topology/physical_package_id", i);

        std::ifstream ifs(filename, std::ios::in);
        if (!ifs)
        {
            // file does not exist
            if (errno == ENOENT)
                break;
            return error(error_code::SYSTEM, system_error_str(
                cmmn::concat("Error opening ", filename)));
        }
        ifs >> pkg;
        if (!ifs)
            return error(error_code::SYSTEM, system_error_str("Error reading package"));
        if (pkg >= max_sockets)
            return error(error_code::TOO_MANY_SOCKETS,
                cmmn::concat("Too many sockets: maximum of ", std::to_string(nrgprf::max_sockets),
                    ", found ", std::to_string(pkg)));
        if (!pkg_found[pkg])
        {
            pkg_found[pkg] = true;
            ret++;
        }
    };
    if (ret == 0)
        return error(error_code::NO_SOCKETS, "No sockets found");
    return ret;
}

#if defined NRG_X86_64

constexpr static const char EVENT_PKG_PREFIX[] = "package";
constexpr static const char EVENT_PP0[] = "core";
constexpr static const char EVENT_PP1[] = "uncore";
constexpr static const char EVENT_DRAM[] = "dram";

namespace nrgprf
{
    namespace detail
    {
        struct file_descriptor
        {
            static result<file_descriptor> create(const char* file);

            int value;

            file_descriptor(const char* file, error& err);
            ~file_descriptor() noexcept;

            file_descriptor(const file_descriptor& fd) noexcept;
            file_descriptor& operator=(const file_descriptor& other) noexcept;

            file_descriptor(file_descriptor&& fd) noexcept;
            file_descriptor& operator=(file_descriptor&& other) noexcept;
        };

        struct event_data
        {
            file_descriptor fd;
            mutable uint64_t max;
            mutable uint64_t prev;
            mutable uint64_t curr_max;
            event_data(const file_descriptor& fd, uint64_t max);
            event_data(file_descriptor&& fd, uint64_t max);
        };
    }
}

// begin helper functions


ssize_t read_buff(int fd, char* buffer, size_t buffsz)
{
    ssize_t ret;
    ret = pread(fd, buffer, buffsz - 1, 0);
    if (ret > 0)
        buffer[ret] = '\0';
    return ret;
}


int read_uint64(int fd, uint64_t* res)
{
    constexpr static const size_t MAX_UINT64_SZ = 24;
    char buffer[MAX_UINT64_SZ];
    char* end;
    if (read_buff(fd, buffer, MAX_UINT64_SZ) <= 0)
        return -1;
    *res = static_cast<uint64_t>(strtoull(buffer, &end, 0));
    if (buffer != end && errno != ERANGE)
        return 0;
    return -1;
}


int32_t domain_index_from_name(const char* name)
{
    if (!strncmp(EVENT_PKG_PREFIX, name, sizeof(EVENT_PKG_PREFIX) - 1))
        return loc::pkg::value;
    if (!strncmp(EVENT_PP0, name, sizeof(EVENT_PP0) - 1))
        return loc::cores::value;
    if (!strncmp(EVENT_PP1, name, sizeof(EVENT_PP1) - 1))
        return loc::uncore::value;
    if (!strncmp(EVENT_DRAM, name, sizeof(EVENT_DRAM) - 1))
        return loc::mem::value;
    return -1;
}


result<int32_t> get_domain_idx(const char* base)
{
    // open the */name file, read the name and obtain the domain index

    char name[64];
    char filename[256];
    snprintf(filename, sizeof(filename), "%s/name", base);
    result<detail::file_descriptor> filed = detail::file_descriptor::create(filename);
    if (!filed)
        return std::move(filed.error());
    if (read_buff(filed.value().value, name, sizeof(name)) < 0)
        return error(error_code::SYSTEM, system_error_str(filename));
    int32_t didx = domain_index_from_name(name);
    if (didx < 0)
        return error(error_code::INVALID_DOMAIN_NAME, cmmn::concat("invalid domain name - ", name));
    return didx;
}


result<detail::event_data> get_event_data(const char* base)
{
    // open the */max_energy_range_uj file and save the max value
    // open the */energy_uj file and save the file descriptor

    char filename[256];
    snprintf(filename, sizeof(filename), "%s/max_energy_range_uj", base);
    result<detail::file_descriptor> filed = detail::file_descriptor::create(filename);
    if (!filed)
        return std::move(filed.error());
    uint64_t max_value;
    if (read_uint64(filed.value().value, &max_value) < 0)
        return error(error_code::SYSTEM, system_error_str(filename));
    snprintf(filename, sizeof(filename), "%s/energy_uj", base);
    filed = detail::file_descriptor::create(filename);
    if (!filed)
        return std::move(filed.error());
    return { std::move(filed.value()), max_value };
}

// end helper functions

// file_descriptor

result<detail::file_descriptor> detail::file_descriptor::create(const char* file)
{
    nrgprf::error err = error::success();
    file_descriptor fd(file, err);
    if (err)
        return err;
    return fd;
}

detail::file_descriptor::file_descriptor(const char* file, error& err) :
    value(open(file, O_RDONLY))
{
    if (value == -1)
        err = { error_code::SYSTEM, system_error_str(file) };
}

detail::file_descriptor::file_descriptor(const file_descriptor& other) noexcept :
    value(dup(other.value))
{
    if (value == -1)
        perror("file_descriptor: error duplicating file descriptor");
}


detail::file_descriptor::file_descriptor(file_descriptor&& other) noexcept :
    value(std::exchange(other.value, -1))
{}

detail::file_descriptor::~file_descriptor() noexcept
{
    if (value >= 0 && close(value) == -1)
        perror("file_descriptor: error closing file");
}

detail::file_descriptor& detail::file_descriptor::operator=(file_descriptor&& other) noexcept
{
    value = other.value;
    other.value = -1;
    return *this;
}

detail::file_descriptor& detail::file_descriptor::operator=(const file_descriptor& other) noexcept
{
    value = dup(other.value);
    if (value == -1)
        perror("file_descriptor: error duplicating file descriptor");
    return *this;
}


// event_data

detail::event_data::event_data(const file_descriptor& fd, uint64_t max) :
    fd(fd),
    max(max),
    prev(0),
    curr_max(0)
{}


detail::event_data::event_data(file_descriptor&& fd, uint64_t max) :
    fd(std::move(fd)),
    max(max),
    prev(0),
    curr_max(0)
{}

#elif defined NRG_PPC64

#ifdef NRG_OCC_USE_DUMMY_FILE
constexpr const char sensors_file[] = NRG_OCC_USE_DUMMY_FILE;
#else
constexpr const char sensors_file[] = "/sys/firmware/opal/exports/occ_inband_sensors";
#endif

namespace occ
{
    // Specification reference
    // https://raw.githubusercontent.com/open-power/docs/master/occ/OCC_P9_FW_Interfaces.pdf

    constexpr const size_t max_count = 8;
    constexpr const size_t bar2_offset = 0x580000;
    constexpr const size_t sensor_data_block_size = 150 * 1024; // 150 kB

    constexpr const size_t sensor_header_version = 1;
    constexpr const size_t sensor_data_header_block_offset = 0;
    constexpr const size_t sensor_data_header_block_size = 1024; // 1 kB
    constexpr const size_t sensor_names_offset = 0x400;
    constexpr const size_t sensor_names_size = 50 * 1024; // 50 kB

    constexpr const size_t sensor_readings_size = 40 * 1024; // 40 kB
    constexpr const size_t sensor_buffer_gap = 4096; // 4 kB

    constexpr const size_t sensor_ping_buffer_offset = 0xdc00;
    constexpr const size_t sensor_ping_buffer_size = sensor_readings_size;
    constexpr const size_t sensor_pong_buffer_offset = 0x18c00;
    constexpr const size_t sensor_pong_buffer_size = sensor_ping_buffer_size;

    enum class sensor_type : uint16_t
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

    enum class sensor_loc : uint16_t
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

    bool assert_sensor_type(sensor_type type)
    {
        switch (type)
        {
        case sensor_type::generic:
        case sensor_type::current:
        case sensor_type::voltage:
        case sensor_type::temp:
        case sensor_type::util:
        case sensor_type::time:
        case sensor_type::freq:
        case sensor_type::power:
        case sensor_type::perf:
            return true;
        }
        assert(false);
        return false;
    }

    bool assert_sensor_location(sensor_loc loc)
    {
        switch (loc)
        {
        case sensor_loc::system:
        case sensor_loc::proc:
        case sensor_loc::partition:
        case sensor_loc::memory:
        case sensor_loc::vrm:
        case sensor_loc::occ:
        case sensor_loc::core:
        case sensor_loc::gpu:
        case sensor_loc::quad:
            return true;
        }
        assert(false);
        return false;
    }

    std::ostream& operator<<(std::ostream& os, sensor_type type)
    {
        using cast_type = std::underlying_type_t<sensor_type>;
        switch (type)
        {
        case sensor_type::generic:
            os << "generic[" << static_cast<cast_type>(sensor_type::generic) << "]";
            break;
        case sensor_type::current:
            os << "current[" << static_cast<cast_type>(sensor_type::current) << "]";
            break;
        case sensor_type::voltage:
            os << "voltage[" << static_cast<cast_type>(sensor_type::voltage) << "]";
            break;
        case sensor_type::temp:
            os << "temp[" << static_cast<cast_type>(sensor_type::temp) << "]";
            break;
        case sensor_type::util:
            os << "util[" << static_cast<cast_type>(sensor_type::util) << "]";
            break;
        case sensor_type::time:
            os << "time[" << static_cast<cast_type>(sensor_type::time) << "]";
            break;
        case sensor_type::freq:
            os << "freq[" << static_cast<cast_type>(sensor_type::freq) << "]";
            break;
        case sensor_type::power:
            os << "power[" << static_cast<cast_type>(sensor_type::power) << "]";
            break;
        case sensor_type::perf:
            os << "perf[" << static_cast<cast_type>(sensor_type::perf) << "]";
            break;
        default:
            assert(false);
            os << "unknown sensor type";
            break;
        }
        return os;
    }

    std::ostream& operator<<(std::ostream& os, sensor_loc loc)
    {
        using cast_type = std::underlying_type_t<sensor_type>;
        switch (loc)
        {
        case sensor_loc::system:
            os << "system[" << static_cast<cast_type>(sensor_loc::system) << "]";
            break;
        case sensor_loc::proc:
            os << "proc[" << static_cast<cast_type>(sensor_loc::proc) << "]";
            break;
        case sensor_loc::partition:
            os << "partition[" << static_cast<cast_type>(sensor_loc::partition) << "]";
            break;
        case sensor_loc::memory:
            os << "memory[" << static_cast<cast_type>(sensor_loc::memory) << "]";
            break;
        case sensor_loc::vrm:
            os << "vrm[" << static_cast<cast_type>(sensor_loc::vrm) << "]";
            break;
        case sensor_loc::occ:
            os << "occ[" << static_cast<cast_type>(sensor_loc::occ) << "]";
            break;
        case sensor_loc::core:
            os << "core[" << static_cast<cast_type>(sensor_loc::core) << "]";
            break;
        case sensor_loc::gpu:
            os << "gpu[" << static_cast<cast_type>(sensor_loc::gpu) << "]";
            break;
        case sensor_loc::quad:
            os << "quad[" << static_cast<cast_type>(sensor_loc::quad) << "]";
            break;
        default:
            assert(false);
            os << "unknown sensor location";
            break;
        }
        return os;
    }

    struct sensor_data_header_block
    {
        constexpr static const size_t size = 24;
        uint8_t valid;
        uint8_t header_version;
        uint16_t sensor_count;
        uint8_t readings_version;
        uint32_t names_offset;
        uint8_t names_version;
        uint8_t name_length;
        uint32_t readings_ping_buffer_offset;
        uint32_t readings_pong_buffer_offset;
    };

    struct sensor_names_entry
    {
        constexpr static const size_t size = 48;
        constexpr static const size_t name_sz = 16;
        constexpr static const size_t unit_sz = 4;
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

    struct sensor_structure_common
    {
        using time_point = std::chrono::time_point<std::chrono::high_resolution_clock>;
        uint16_t gsid;
        uint64_t timestamp;
    };

    struct sensor_structure_v1 : sensor_structure_common
    {
        constexpr static const size_t size = 48;
        uint16_t sample;
        uint16_t sample_min;
        uint16_t sample_max;
        uint16_t csm_sample_min;
        uint16_t csm_sample_max;
        uint16_t profiler_sample_min;
        uint16_t profiler_sample_max;
        uint16_t job_s_sample_min;
        uint16_t job_s_sample_max;
        uint64_t accumulator;
        uint32_t update_tag;
    };

    struct sensor_structure_v1_sample
    {
        constexpr static const size_t size = 10;
        uint64_t timestamp;
        uint16_t sample;
    };

    struct sensor_structure_v2 : sensor_structure_common
    {
        constexpr static const size_t size = 24;
        uint64_t accumulator;
        uint8_t sample;
    };

    union sensor_structure
    {
        sensor_structure_v1 sv1;
        sensor_structure_v2 sv2;
    };

    struct sensor_readings_buffer
    {
        constexpr static const size_t size = sensor_readings_size;
        constexpr static const size_t pad = 8;
        uint8_t valid;
        uint8_t readings[size - pad]; // valid byte and 7 bytes reserved
    };

    struct sensor_buffers
    {
        constexpr static const size_t size = sensor_pong_buffer_offset -
            sensor_ping_buffer_offset + sensor_pong_buffer_size;

        static_assert(size ==
            sensor_ping_buffer_size + sensor_pong_buffer_size + sensor_buffer_gap);

        sensor_readings_buffer ping;
        sensor_readings_buffer pong;
    };

    namespace detail
    {
        template<typename T>
        std::string to_hex_string(const T& arg)
        {
            std::ostringstream ss;
            ss << std::hex << arg;
            return ss.str();
        }

        static double to_double(uint32_t val)
        {
            return (val >> 8) * std::pow(10, static_cast<int8_t>(val & 0xff));
        }

        static void copy_readings_buffer(
            const std::istream::char_type* from,
            sensor_readings_buffer& into)
        {
            into.valid = *from;
            std::memcpy(into.readings, from + 8, sizeof(into.readings)); // 7 bytes reserved
        }

        template<typename T, size_t sz = sizeof(T)>
        static size_t retrieve_field(T* into, const void* from)
        {
            if constexpr (sz > 1)
            {
                std::memcpy(into, from, sz);
                if constexpr (sz == 2)
                    *into = be16toh(*into);
                else if constexpr (sz == 4)
                    *into = be32toh(*into);
                else if constexpr (sz == 8)
                    *into = be64toh(*into);
            }
            else
                *into = *static_cast<const T*>(from);
            return sz;
        }

        template<size_t sz = sizeof(sensor_type)>
        static size_t retrieve_field(sensor_type* into, const void* from)
        {
            std::memcpy(into, from, sz);
            *into = static_cast<sensor_type>(
                be16toh(static_cast<std::underlying_type_t<sensor_type>>(*into)));
            return sz;
        }

        template<size_t sz = sizeof(sensor_loc)>
        static size_t retrieve_field(sensor_loc* into, const void* from)
        {
            std::memcpy(into, from, sz);
            *into = static_cast<sensor_loc>(
                be16toh(static_cast<std::underlying_type_t<sensor_loc>>(*into)));
            return sz;
        }

        template<size_t sz>
        static size_t retrieve_field(char* into, const void* from)
        {
            if constexpr (sz > 1)
                std::memcpy(into, from, sz);
            else
                *into = *static_cast<const char*>(from);
            return sz;
        }
    }

    std::istream& operator>>(std::istream& is, sensor_data_header_block& hb)
    {
        using namespace detail;
        std::istream::char_type buffer[sensor_data_header_block::size];
        std::istream::char_type* curr = buffer;
        if (!is.read(buffer, sizeof(buffer)))
            return is;

        curr += retrieve_field(&hb.valid, curr);
        curr += retrieve_field(&hb.header_version, curr);
        curr += retrieve_field(&hb.sensor_count, curr);
        curr += retrieve_field(&hb.readings_version, curr);
        curr += 3; // reserved
        curr += retrieve_field(&hb.names_offset, curr);
        curr += retrieve_field(&hb.names_version, curr);
        curr += retrieve_field(&hb.name_length, curr);
        curr += 2; // reserved
        curr += retrieve_field(&hb.readings_ping_buffer_offset, curr);
        retrieve_field(&hb.readings_pong_buffer_offset, curr);
        return is;
    }

    bool assert_header_block(const sensor_data_header_block& hb, std::string& msg)
    {
        assert(hb.valid);
        assert(hb.header_version == sensor_header_version);
        assert(hb.names_offset == sensor_names_offset);
        assert(hb.readings_ping_buffer_offset == sensor_ping_buffer_offset);
        assert(hb.readings_pong_buffer_offset == sensor_pong_buffer_offset);
        if (!hb.valid)
            msg = cmmn::concat("Header block not valid; static data not ready for consumption");

        else if (hb.header_version != sensor_header_version)
            msg = cmmn::concat("Unsupported header version, expected ",
                std::to_string(sensor_header_version), ", found ",
                std::to_string(+hb.header_version));

        else if (hb.names_offset != sensor_names_offset)
            msg = cmmn::concat("Incorrect names offset, expected 0x",
                detail::to_hex_string(sensor_names_offset), ", found 0x",
                detail::to_hex_string(hb.names_offset));

        else if (hb.readings_ping_buffer_offset != sensor_ping_buffer_offset)
            msg = cmmn::concat("Incorrect ping buffer offset, expected 0x",
                detail::to_hex_string(sensor_ping_buffer_offset), ", found 0x",
                detail::to_hex_string(hb.readings_ping_buffer_offset));

        else if (hb.readings_pong_buffer_offset != sensor_pong_buffer_offset)
            msg = cmmn::concat("Incorrect pong buffer offset, expected 0x",
                detail::to_hex_string(sensor_pong_buffer_offset), ", found 0x",
                detail::to_hex_string(hb.readings_pong_buffer_offset));

        else
            return true;
        return false;
    }

    std::istream& operator>>(std::istream& is, sensor_names_entry& entry)
    {
        using namespace detail;
        std::istream::char_type buffer[sensor_names_entry::size];
        std::istream::char_type* curr = buffer;
        if (!is.read(buffer, sizeof(buffer)))
            return is;

        curr += retrieve_field<sizeof(entry.name)>(entry.name, curr);
        curr += retrieve_field<sizeof(entry.units)>(entry.units, curr);
        curr += retrieve_field(&entry.gsid, curr);

        uint32_t placeholder;
        curr += retrieve_field(&placeholder, curr);
        entry.freq = to_double(placeholder);
        curr += retrieve_field(&placeholder, curr);
        entry.scaling_factor = to_double(placeholder);

        curr += retrieve_field(&entry.type, curr);
        curr += retrieve_field(&entry.location, curr);
        curr += retrieve_field(&entry.structure_version, curr);
        curr += retrieve_field(&entry.reading_offset, curr);
        retrieve_field(&entry.specific_info1, curr);

        // null-terminate name and units fields, just in case
        entry.name[sizeof(entry.name) - 1] = '\0';
        entry.units[sizeof(entry.units) - 1] = '\0';
        return is;
    }

    bool assert_names_entry(const sensor_names_entry& entry, std::string& msg)
    {
        assert(entry.structure_version == 1 || entry.structure_version == 2);
        assert(assert_sensor_location(entry.location));
        assert(assert_sensor_type(entry.type));
        if (!assert_sensor_location(entry.location))
            msg = "Unknown sensor location";
        else if (!assert_sensor_type(entry.type))
            msg = "Unknown sensor type";
        else if (entry.structure_version != 1 && entry.structure_version != 2)
            msg = cmmn::concat("Unsupported structure version, expected 1 or 2, found ",
                std::to_string(+entry.structure_version));
        else
            return true;
        return false;
    }

    std::istream& operator>>(std::istream& is, sensor_readings_buffer& srb)
    {
        std::istream::char_type buffer[sensor_readings_buffer::size];
        if (is.read(buffer, sizeof(buffer)))
            detail::copy_readings_buffer(buffer, srb);
        return is;
    }

    std::istream& operator>>(std::istream& is, sensor_buffers& buffs)
    {
        std::istream::char_type buffer[sensor_buffers::size];
        if (is.read(buffer, sizeof(buffer)))
        {
            detail::copy_readings_buffer(buffer, buffs.ping);
            detail::copy_readings_buffer(buffer + sensor_readings_size + sensor_buffer_gap,
                buffs.pong);
        }
        return is;
    }

    sensor_structure_v1_sample get_v1_sample(const sensor_readings_buffer& buffer, size_t offset)
    {
        assert(buffer.valid);
        sensor_structure_v1_sample ret;

        // skip the first field gsid, assertions are done during initial parse
        const uint8_t* curr = buffer.readings + offset -
            sensor_readings_buffer::pad + sizeof(sensor_structure_v1::gsid);

        curr += detail::retrieve_field(&ret.timestamp, curr);
        detail::retrieve_field(&ret.sample, curr);
        return ret;
    }

    sensor_structure_v1 get_sensor_structure_v1(const sensor_readings_buffer& buffer, size_t offset)
    {
        assert(buffer.valid);
        using namespace detail;
        sensor_structure_v1 ret;
        const uint8_t* curr = buffer.readings + offset - sensor_readings_buffer::pad;

        curr += retrieve_field(&ret.gsid, curr);
        curr += retrieve_field(&ret.timestamp, curr);
        curr += retrieve_field(&ret.sample, curr);
        curr += retrieve_field(&ret.sample_min, curr);
        curr += retrieve_field(&ret.sample_max, curr);
        curr += retrieve_field(&ret.csm_sample_min, curr);
        curr += retrieve_field(&ret.csm_sample_max, curr);
        curr += retrieve_field(&ret.profiler_sample_min, curr);
        curr += retrieve_field(&ret.profiler_sample_max, curr);
        curr += retrieve_field(&ret.job_s_sample_min, curr);
        curr += retrieve_field(&ret.job_s_sample_max, curr);
        curr += retrieve_field(&ret.accumulator, curr);
        retrieve_field(&ret.update_tag, curr);
        return ret;
    }

    sensor_structure_v2 get_sensor_structure_v2(const sensor_readings_buffer& buffer, size_t offset)
    {
        assert(buffer.valid);
        using namespace detail;
        sensor_structure_v2 ret;
        const uint8_t* curr = buffer.readings + offset - sensor_readings_buffer::pad;

        curr += retrieve_field(&ret.gsid, curr);
        curr += retrieve_field(&ret.timestamp, curr);
        curr += retrieve_field(&ret.accumulator, curr);
        curr += retrieve_field(&ret.sample, curr);
        return ret;
    }

    bool get_sensor_structure(
        const sensor_readings_buffer& buffer,
        const sensor_names_entry& entry,
        sensor_structure& out)
    {
        switch (entry.structure_version)
        {
        case 1:
            out.sv1 = get_sensor_structure_v1(buffer, entry.reading_offset);
            return true;
        case 2:
            out.sv2 = get_sensor_structure_v2(buffer, entry.reading_offset);
            return true;
        }
        assert(entry.structure_version == 1 || entry.structure_version == 2);
        return false;
    }

    bool assert_sensor_structure(
        const sensor_structure& sstruct,
        const sensor_names_entry& nentry,
        std::string& msg)
    {
        bool result = false;
        switch (nentry.structure_version)
        {
        case 1:
            result = sstruct.sv1.gsid == nentry.gsid;
            if (!result)
                msg = cmmn::concat("Sensor GSID are different between readings and names entry,"
                    " found ", std::to_string(sstruct.sv1.gsid),
                    " and ", std::to_string(nentry.gsid));
            return result;
        case 2:
            result = sstruct.sv2.gsid != nentry.gsid;
            if (!result)
                msg = cmmn::concat("Sensor GSID are different between readings and names entry,"
                    " found ", std::to_string(sstruct.sv2.gsid),
                    " and ", std::to_string(nentry.gsid));
            return result;
        }
        assert(false);
        return false;
    }

    uint64_t get_timestamp(
        const sensor_readings_buffer& buffer,
        size_t offset)
    {
        assert(buffer.valid);
        uint64_t timestamp;
        const uint8_t* curr = buffer.readings + offset - sensor_readings_buffer::pad;
        detail::retrieve_field(&timestamp, curr);
        return timestamp;
    }

    bool get_sensor_record(
        const sensor_buffers& buffs,
        const sensor_names_entry& entry,
        sensor_structure& out)
    {
        if (buffs.ping.valid && buffs.pong.valid)
        {
            auto ping_ts = get_timestamp(buffs.ping, entry.reading_offset);
            auto pong_ts = get_timestamp(buffs.pong, entry.reading_offset);
            if (ping_ts > pong_ts)
                return get_sensor_structure(buffs.ping, entry, out);
            else
                return get_sensor_structure(buffs.pong, entry, out);
        }
        if (buffs.pong.valid)
            return get_sensor_structure(buffs.pong, entry, out);
        if (buffs.ping.valid)
            return get_sensor_structure(buffs.ping, entry, out);

        assert(buffs.ping.valid || buffs.pong.valid);
        return false;
    }

    std::ostream& operator<<(std::ostream& os, const sensor_data_header_block& hb)
    {
        std::ios::fmtflags flags(os.flags());
        os << std::boolalpha << "valid: " << bool(hb.valid) << "\n";
        os.flags(flags);

        os << "header version: " << +hb.header_version << "\n";
        os << "number of sensors: " << hb.sensor_count << "\n";
        os << "readings version: " << +hb.readings_version << "\n";

        flags = os.flags();
        os << std::hex << "names offset: 0x" << hb.names_offset << "\n";
        os.flags(flags);

        os << "names version: " << +hb.names_version << "\n";
        os << "names length: " << +hb.name_length << "\n";

        flags = os.flags();
        os << std::hex;
        os << "ping buffer offset: 0x" << hb.readings_ping_buffer_offset << "\n";
        os << "pong buffer offset: 0x" << hb.readings_pong_buffer_offset;
        os.flags(flags);

        return os;
    }

    std::ostream& operator<<(std::ostream& os, const sensor_names_entry& ne)
    {
        os << ne.name << ":" << ne.units << ":" << ne.gsid;
        os << ":f=" << ne.freq;
        os << ":s=" << ne.scaling_factor;
        os << ":" << ne.type << ":" << ne.location;
        os << ":v=" << +ne.structure_version;
        std::ios::fmtflags flags(os.flags());
        os << std::hex << ":0x" << ne.reading_offset;
        os.flags(flags);
        os << ":" << +ne.specific_info1;
        return os;
    }

    std::ostream& operator<<(std::ostream& os, const sensor_structure_v1& s)
    {
        os << "v1";
        os << ":" << s.gsid << ":" << s.timestamp;
        os << ":s=" << s.sample << ":m=" << s.sample_min << ":M=" << s.sample_max;
        os << ":csmm=" << s.csm_sample_min << ":csmM=" << s.csm_sample_max;
        os << ":pm=" << s.profiler_sample_min << ":pM=" << s.profiler_sample_max;
        os << ":jm=" << s.job_s_sample_min << ":jM=" << s.job_s_sample_max;
        os << ":a=" << s.accumulator << ":u=" << s.update_tag;
        return os;
    }

    std::ostream& operator<<(std::ostream& os, const sensor_structure_v2& s)
    {
        os << "v2";
        os << ":" << s.gsid << ":" << s.timestamp;
        os << ":a=" << s.accumulator;
        os << ":" << +s.sample;
        return os;
    }

    static_assert(24 == occ::sensor_data_header_block::size,
        "occ::sensor_data_header_block::size != 24");
    static_assert(48 == occ::sensor_names_entry::size,
        "occ::sensor_names_entry::size != 48");
    static_assert(48 == occ::sensor_structure_v1::size,
        "occ::sensor_structure_v1::size != 48");
    static_assert(24 == occ::sensor_structure_v2::size,
        "occ::sensor_structure_v2::size != 24");
    static_assert(40960 == occ::sensor_readings_buffer::size,
        "occ::sensor_readings_buffer::size != 40960");
    static_assert(86016 == occ::sensor_buffers::size,
        "occ::sensor_buffers::size != 86016");
}

#endif // defined NRG_X86_64

#endif // CPU_NONE

// reader_rapl::impl

#if defined CPU_NONE

class reader_rapl::impl
{
public:
    impl(location_mask, socket_mask, error&);

    error read(sample&) const;
    error read(sample&, uint8_t) const;
    size_t num_events() const;

    template<typename Tag>
    int32_t event_idx(uint8_t) const;

    template<typename Location, typename Type>
    result<Type> value(const sample& s, uint8_t) const;
};

#elif defined NRG_X86_64

class reader_rapl::impl
{
    std::array<std::array<int32_t, rapl_domains>, max_sockets> _event_map;
    std::vector<detail::event_data> _active_events;

public:
    impl(location_mask, socket_mask, error&);

    error read(sample&) const;
    error read(sample&, uint8_t) const;
    size_t num_events() const;

    template<typename Tag>
    int32_t event_idx(uint8_t) const;

    template<typename Location, typename Type>
    result<Type> value(const sample& s, uint8_t) const;

private:
    error add_event(const char* base, location_mask dmask, uint8_t skt);
};

#elif defined NRG_PPC64

namespace nrgprf::detail
{

    // TODO: Need to confirm whether sensor GSIDs are constant or dynamically assigned
    // (at reboot, for example)
    static constexpr const uint16_t gsid_pwrsys = 20;
    static constexpr const uint16_t gsid_pwrgpu = 24;
    static constexpr const uint16_t gsid_pwrproc = 48;
    static constexpr const uint16_t gsid_pwrmem = 49;
    static constexpr const uint16_t gsid_pwrvdd = 56;
    static constexpr const uint16_t gsid_pwrvdn = 57;

    struct sensor_static_data
    {
        uint16_t gsid;
        occ::sensor_type type;
        occ::sensor_loc loc;
    };

    static constexpr const sensor_static_data bit_to_sensor_data[] =
    {
        { gsid_pwrproc, occ::sensor_type::power, occ::sensor_loc::proc },
        { gsid_pwrvdd,  occ::sensor_type::power, occ::sensor_loc::proc },
        { gsid_pwrvdn,  occ::sensor_type::power, occ::sensor_loc::proc },
        { gsid_pwrmem,  occ::sensor_type::power, occ::sensor_loc::memory },
        { gsid_pwrsys,  occ::sensor_type::power, occ::sensor_loc::system },
        { gsid_pwrgpu,  occ::sensor_type::power, occ::sensor_loc::gpu }
    };

    static constexpr int32_t sensor_gsid_to_index(uint16_t gsid)
    {
        switch (gsid)
        {
        case gsid_pwrsys:
            return loc::sys::value;
        case gsid_pwrgpu:
            return loc::gpu::value;
        case gsid_pwrproc:
            return loc::pkg::value;
        case gsid_pwrmem:
            return loc::mem::value;
        case gsid_pwrvdd:
            return loc::cores::value;
        case gsid_pwrvdn:
            return loc::nest::value;
        default:
            assert(false);
            return -1;
        }
    }

    template<typename T>
    static constexpr uint16_t to_sensor_gsid()
    {
        static_assert(std::is_same_v<T, loc::sys> ||
            std::is_same_v<T, loc::pkg> ||
            std::is_same_v<T, loc::cores> ||
            std::is_same_v<T, loc::nest> ||
            std::is_same_v<T, loc::mem> ||
            std::is_same_v<T, loc::gpu>,
            "T must be of type sys, pkg, cores, nest, mem or gpu");
        if constexpr (std::is_same_v<T, loc::sys>)
            return gsid_pwrsys;
        if constexpr (std::is_same_v<T, loc::gpu>)
            return gsid_pwrgpu;
        if constexpr (std::is_same_v<T, loc::pkg>)
            return gsid_pwrproc;
        if constexpr (std::is_same_v<T, loc::cores>)
            return gsid_pwrvdd;
        if constexpr (std::is_same_v<T, loc::nest>)
            return gsid_pwrvdn;
        if constexpr (std::is_same_v<T, loc::mem>)
            return gsid_pwrmem;
    }

    static watts<double> canonicalize_power(uint16_t value, const occ::sensor_names_entry& entry)
    {
        if (std::string_view(entry.units) == "W")
            return watts<double>(value * entry.scaling_factor);
        else
        {
            assert(false);
            return watts<double>{};
        }
    };

    struct event_data
    {
        uint32_t occ_num;
        std::vector<occ::sensor_names_entry> entries;
    };
}

class reader_rapl::impl
{
    std::array<std::array<int8_t, occ_domains>, max_sockets> _event_map;
    std::vector<detail::event_data> _active_events;

public:
    impl(location_mask, socket_mask, error&);

    error read(sample&) const;
    error read(sample&, uint8_t) const;
    size_t num_events() const;

    template<typename Tag>
    int32_t event_idx(uint8_t) const;

    template<typename Location, typename Type>
    result<Type> value(const sample& s, uint8_t) const;

private:
    error get_header(std::ifstream& ifs,
        uint32_t occ_num,
        occ::sensor_data_header_block& hb) const;

    error get_names_entries(std::ifstream& ifs,
        uint32_t occ_num,
        std::vector<occ::sensor_names_entry>& entries) const;

    error get_sensor_buffers(std::ifstream& ifs,
        uint32_t occ_num,
        occ::sensor_buffers& buffs) const;

    error get_sensor_structs(const occ::sensor_buffers& buffs,
        const std::vector<occ::sensor_names_entry>& entries,
        std::vector<occ::sensor_structure>& structs) const;

    error add_event(const std::vector<occ::sensor_names_entry>& entries,
        uint32_t occ_num,
        uint32_t location);
};

#endif

#if defined CPU_NONE

reader_rapl::impl::impl(location_mask, socket_mask, error&)
{
    std::cout << fileline("No-op CPU reader\n");
}

error reader_rapl::impl::read(sample&) const
{
    return error::success();
}

error reader_rapl::impl::read(sample&, uint8_t) const
{
    return error::success();
}

size_t reader_rapl::impl::num_events() const
{
    return 0;
}

template<typename Tag>
int32_t reader_rapl::impl::event_idx(uint8_t) const
{
    return -1;
}

template<typename Location, typename Type>
result<Type> reader_rapl::impl::value(const sample&, uint8_t) const
{
    return error(error_code::NO_EVENT);
}

#elif defined NRG_X86_64

reader_rapl::impl::impl(location_mask dmask, socket_mask skt_mask, error& ec) :
    _event_map(),
    _active_events()
{
    for (auto& skts : _event_map)
        skts.fill(-1);

    result<uint8_t> num_skts = count_sockets();
    if (!num_skts)
    {
        ec = std::move(num_skts.error());
        return;
    }
    std::cout << fileline(cmmn::concat("found ", std::to_string(num_skts.value()), " sockets\n"));
    for (uint8_t skt = 0; skt < num_skts.value(); skt++)
    {
        if (!skt_mask[skt])
            continue;
        std::cout << fileline(cmmn::concat("registered socket: ", std::to_string(skt), "\n"));

        char base[96];
        int written = snprintf(base, sizeof(base), "/sys/class/powercap/intel-rapl/intel-rapl:%u", skt);
        error err = add_event(base, dmask, skt);
        if (err)
        {
            ec = std::move(err);
            return;
        }
        // already found one domain above
        for (uint8_t domain_count = 0; domain_count < rapl_domains - 1; domain_count++)
        {
            snprintf(base + written, sizeof(base) - written, "/intel-rapl:%u:%u", skt, domain_count);
            // only consider the domain if the file exists
            if (access(base, F_OK) != -1)
            {
                err = add_event(base, dmask, skt);
                if (err)
                {
                    ec = std::move(err);
                    return;
                }
            }
        }
    }
}

error reader_rapl::impl::read(sample& s) const
{
    for (size_t ix = 0; ix < _active_events.size(); ix++)
    {
        error err = read(s, ix);
        if (err)
            return err;
    };
    return error::success();
}

error reader_rapl::impl::read(sample& s, uint8_t ev_idx) const
{
    uint64_t curr;
    if (read_uint64(_active_events[ev_idx].fd.value, &curr) == -1)
        return { error_code::SYSTEM, system_error_str("Error reading counters") };
    if (curr < _active_events[ev_idx].prev)
    {
        std::cout << fileline("reader_rapl: detected wraparound\n");
        _active_events[ev_idx].curr_max += _active_events[ev_idx].max;
    }
    _active_events[ev_idx].prev = curr;
    s.cpu[ev_idx] = curr + _active_events[ev_idx].curr_max;
    return error::success();
}

size_t reader_rapl::impl::num_events() const
{
    return _active_events.size();
}

template<typename Location>
int32_t reader_rapl::impl::event_idx(uint8_t skt) const
{
    return _event_map[skt][Location::value];
}

template<typename Location, typename Type>
result<Type> reader_rapl::impl::value(const sample& s, uint8_t skt) const
{
    if (event_idx<Location>(skt) < 0)
        return error(error_code::NO_EVENT);
    auto result = s.cpu[event_idx<Location>(skt)];
    if (!result)
        return error(error_code::NO_EVENT);
    return result;
}

error reader_rapl::impl::add_event(const char* base, location_mask dmask, uint8_t skt)
{
    result<int32_t> didx = get_domain_idx(base);
    if (!didx)
        return std::move(didx.error());
    if (dmask[didx.value()])
    {
        result<detail::event_data> event_data = get_event_data(base);
        if (!event_data)
            return std::move(event_data.error());
        std::cout << fileline(cmmn::concat("added event: ", base, "\n"));
        _event_map[skt][didx.value()] = _active_events.size();
        _active_events.push_back(std::move(event_data.value()));
    }
    return error::success();
}

#elif defined NRG_PPC64

reader_rapl::impl::impl(location_mask lmask, socket_mask smask, error& err) :
    _event_map(),
    _active_events()
{
    std::ifstream ifs(sensors_file, std::ios::in | std::ios::binary);
    if (!ifs)
    {
        err = { error_code::SYSTEM, system_error_str(
            cmmn::concat("Error opening ", sensors_file)) };
        return;
    }

    for (auto& skts : _event_map)
        skts.fill(-1);

    result<uint8_t> sockets = count_sockets();
    if (!sockets)
    {
        err = std::move(sockets.error());
        return;
    }
    std::cout << fileline(cmmn::concat("Found ", std::to_string(sockets.value()), " sockets\n"));
    for (uint32_t occ_num = 0; occ_num < sockets.value(); occ_num++)
    {
        if (!smask[occ_num])
            continue;
        std::cout << fileline(cmmn::concat("Registered socket: ", std::to_string(occ_num), "\n"));

        occ::sensor_data_header_block hb{};
        if (err = get_header(ifs, occ_num, hb))
            return;

        std::vector<occ::sensor_names_entry> entries(hb.sensor_count, occ::sensor_names_entry{});
        if (err = get_names_entries(ifs, occ_num, entries))
            return;

        occ::sensor_buffers sbuffs{};
        if (err = get_sensor_buffers(ifs, occ_num, sbuffs))
            return;

        std::vector<occ::sensor_structure> structs;
        structs.reserve(entries.size());
        if (err = get_sensor_structs(sbuffs, entries, structs))
            return;

        for (uint32_t loc = 0; loc < nrgprf::max_domains; loc++)
        {
            if (!lmask[loc])
                continue;
            // the system power sensor only exists in the master OCC which is OCC 0
            if (occ_num != 0 && detail::bit_to_sensor_data[loc].gsid == detail::gsid_pwrsys)
                continue;
            add_event(entries, occ_num, loc);
        }
    }

    if (!num_events())
        err = { error_code::SETUP_ERROR, "No events were added" };
}

error reader_rapl::impl::get_header(std::ifstream& ifs,
    uint32_t occ_num,
    occ::sensor_data_header_block& hb) const
{
    assert(occ_num < occ::max_count);

    size_t occ_offset = occ_num * occ::sensor_data_block_size;
    std::string occ_num_str = std::to_string(occ_num);

    ifs.seekg(occ_offset + occ::sensor_data_header_block_offset);
    if (!ifs)
        return { error_code::SYSTEM, system_error_str(
            cmmn::concat("Error seeking to OCC ", occ_num_str, " header")) };

    if (!(ifs >> hb))
    {
        std::string msg;
        if (ifs.eof())
            msg = cmmn::concat("Reached end-of-stream before reading header block of OCC ",
                occ_num_str);
        else if (ifs.bad())
            msg = system_error_str(cmmn::concat("Error reading header of OCC ", occ_num_str));
        else
            msg = cmmn::concat("Error reading header of OCC ", occ_num_str);
        return { error_code::SYSTEM, std::move(msg) };
    }

    if (std::string msg; !occ::assert_header_block(hb, msg))
        return { error_code::FORMAT_ERROR, std::move(msg) };

    std::cout << hb << std::endl;
    return error::success();
}

error reader_rapl::impl::get_names_entries(std::ifstream& ifs,
    uint32_t occ_num,
    std::vector<occ::sensor_names_entry>& entries) const
{
    assert(occ_num < occ::max_count);
    size_t occ_offset = occ_num * occ::sensor_data_block_size;
    std::string occ_num_str = std::to_string(occ_num);

    ifs.seekg(occ_offset + occ::sensor_names_offset);
    if (!ifs)
        return { error_code::SYSTEM, system_error_str(
            cmmn::concat("Error seeking to OCC ", occ_num_str, " sensor names")) };
    for (auto& entry : entries)
    {
        if (!(ifs >> entry))
        {
            std::string msg;
            if (ifs.eof())
                msg = cmmn::concat("Reached end-of-stream before"
                    " reading sensor names entries of OCC ", occ_num_str);
            else if (ifs.bad())
                msg = system_error_str(cmmn::concat("Error reading sensor name entry of OCC ",
                    occ_num_str));
            else
                msg = cmmn::concat("Error reading sensor name entry of OCC ", occ_num_str);
            return { error_code::SYSTEM, std::move(msg) };
        }
        if (std::string msg; !occ::assert_names_entry(entry, msg))
            return { error_code::FORMAT_ERROR, std::move(msg) };
    }
    return error::success();
}

error reader_rapl::impl::get_sensor_buffers(std::ifstream& ifs,
    uint32_t occ_num,
    occ::sensor_buffers& buffs) const
{
    assert(occ_num < occ::max_count);
    size_t occ_offset = occ_num * occ::sensor_data_block_size;
    std::string occ_num_str = std::to_string(occ_num);

    ifs.seekg(occ_offset + occ::sensor_ping_buffer_offset);
    if (!ifs)
        return { error_code::SYSTEM, system_error_str(
            cmmn::concat("Error seeking to OCC ", occ_num_str, " sensor names")) };

    if (!(ifs >> buffs))
    {
        std::string msg;
        if (ifs.eof())
            msg = cmmn::concat("Reached end-of-stream before"
                " reading sensor buffers of OCC ", occ_num_str);
        else if (ifs.bad())
            msg = system_error_str(cmmn::concat("Error reading sensor buffers of OCC ",
                occ_num_str));
        else
            msg = cmmn::concat("Error reading sensor buffers of OCC ", occ_num_str);
        return { error_code::SYSTEM, std::move(msg) };
    }
    return error::success();
}


error reader_rapl::impl::get_sensor_structs(const occ::sensor_buffers& buffs,
    const std::vector<occ::sensor_names_entry>& entries,
    std::vector<occ::sensor_structure>& structs) const
{
    for (const auto& entry : entries)
    {
        occ::sensor_structure sstruct;
        if (!occ::get_sensor_record(buffs, entry, sstruct))
            return { error_code::FORMAT_ERROR, "Both ping and pong buffers are not valid" };
        std::cout << entry << "\n";
        switch (entry.structure_version)
        {
        case 1:
            std::cout << "  " << sstruct.sv1 << "\n";
            break;
        case 2:
            std::cout << "  " << sstruct.sv2 << "\n";
            break;
        default:
            assert(false);
            return { error_code::FORMAT_ERROR, "Unsupported sensor structure version found" };
        }
        structs.push_back(sstruct);
    }
    return error::success();
}

error reader_rapl::impl::add_event(const std::vector<occ::sensor_names_entry>& entries,
    uint32_t occ_num,
    uint32_t loc)
{
    const detail::sensor_static_data& sensor_data = detail::bit_to_sensor_data[loc];
    _event_map[occ_num][loc] = _active_events.size();
    _active_events.push_back({ occ_num, std::vector<occ::sensor_names_entry>() });
    for (const auto& entry : entries)
    {
        auto& active_entries = _active_events.back().entries;
        if (entry.gsid == sensor_data.gsid &&
            entry.type == sensor_data.type &&
            entry.location == sensor_data.loc)
        {
            assert(entry.structure_version == 1);
            if (entry.structure_version != 1)
                return { error_code::NOT_IMPL, "Unsupported structure version" };
            active_entries.push_back(entry);
            std::cout << fileline("added event - OCC=") << occ_num << " " << entry << "\n";
        }
    }
    return error::success();
}


error reader_rapl::impl::read(sample& s) const
{
    std::ifstream ifs(sensors_file, std::ios::in | std::ios::binary);
    if (!ifs)
        return { error_code::SYSTEM, system_error_str(
            cmmn::concat("Error opening ", sensors_file)) };
    for (const auto& ed : _active_events)
    {
        occ::sensor_buffers sbuffs;
        if (error err = get_sensor_buffers(ifs, ed.occ_num, sbuffs))
            return err;
        for (const auto& entry : ed.entries)
        {
            size_t stride = ed.occ_num * nrgprf::max_domains +
                detail::sensor_gsid_to_index(entry.gsid);

            // TODO: read from both ping and pong buffers
            occ::sensor_structure_v1_sample record =
                occ::get_v1_sample(sbuffs.pong, entry.reading_offset);

            s.timestamps[stride] = record.timestamp;
            s.cpu[stride] = record.sample;
        }
    }
    return error::success();
}

error reader_rapl::impl::read(sample&, uint8_t) const
{
    return error::success();
}

size_t reader_rapl::impl::num_events() const
{
    return _active_events.size();
}

template<typename Location>
int32_t reader_rapl::impl::event_idx(uint8_t skt) const
{
    return _event_map[skt][Location::value];
}

template<typename Location, typename Type>
result<Type> reader_rapl::impl::value(const sample& s, uint8_t skt) const
{
    assert(skt < max_sockets);
    int32_t idx = event_idx<Location>(skt);
    if (idx < 0)
        return error(error_code::NO_EVENT);
    uint32_t stride = skt * nrgprf::max_domains + Location::value;
    if constexpr (std::is_same_v<Type, units_power>)
    {
        auto result = s.cpu[stride];
        if (!result)
            return error(error_code::NO_EVENT);
        for (const auto& sensor_entry : _active_events[idx].entries)
        {
            if (sensor_entry.gsid == detail::to_sensor_gsid<Location>())
            {
                watts<double> power = detail::canonicalize_power(result, sensor_entry);
                if (!power.count())
                    return error(error_code::NOT_IMPL, "Unsupported power units found");
                return unit_cast<units_power>(power);
            }
        };
        return error(error_code::NO_EVENT);
    }
    else if constexpr (std::is_same_v<Type, units_time>)
    {
        auto result = s.timestamps[stride];
        if (!result)
            return error(error_code::NO_EVENT);
        return units_time::duration{ result };
    }
}

#endif // CPU_NONE

// reader_rapl

reader_rapl::reader_rapl(location_mask dmask, socket_mask skt_mask, error& ec) :
    _impl(std::make_unique<reader_rapl::impl>(dmask, skt_mask, ec))
{}

reader_rapl::reader_rapl(location_mask dmask, error& ec) :
    reader_rapl(dmask, socket_mask(~0x0), ec)
{}

reader_rapl::reader_rapl(socket_mask skt_mask, error& ec) :
    reader_rapl(location_mask(~0x0), skt_mask, ec)
{}

reader_rapl::reader_rapl(error& ec) :
    reader_rapl(location_mask(~0x0), socket_mask(~0x0), ec)
{}

reader_rapl::reader_rapl(const reader_rapl& other) :
    _impl(std::make_unique<reader_rapl::impl>(*other.pimpl()))
{}

reader_rapl& reader_rapl::operator=(const reader_rapl& other)
{
    _impl = std::make_unique<reader_rapl::impl>(*other.pimpl());
    return *this;
}

reader_rapl::reader_rapl(reader_rapl&& other) = default;
reader_rapl& reader_rapl::operator=(reader_rapl && other) = default;
reader_rapl::~reader_rapl() = default;

error reader_rapl::read(sample & s) const
{
    return pimpl()->read(s);
}

error reader_rapl::read(sample & s, uint8_t idx) const
{
    return pimpl()->read(s, idx);
}

size_t reader_rapl::num_events() const
{
    return pimpl()->num_events();
}

template<typename Location>
int32_t reader_rapl::event_idx(uint8_t skt) const
{
    return pimpl()->event_idx<Location>(skt);
}

template<typename Location, typename Type>
result<Type> reader_rapl::value(const sample & s, uint8_t skt) const
{
    return pimpl()->value<Location, Type>(s, skt);
}

template<typename Location, typename Type>
std::vector<std::pair<uint32_t, Type>> reader_rapl::values(const sample & s) const
{
    std::vector<std::pair<uint32_t, Type>> retval;
    for (uint32_t skt = 0; skt < max_sockets; skt++)
    {
        result<Type> val = value<Location, Type>(s, skt);
        if (val)
            retval.push_back({ skt, std::move(val.value()) });
    };
    return retval;
}

const reader_rapl::impl* reader_rapl::pimpl() const
{
    assert(_impl);
    return _impl.get();
}

reader_rapl::impl* reader_rapl::pimpl()
{
    assert(_impl);
    return _impl.get();
}

// template specializations

#ifndef CPU_NONE

#define SPECIALIZE_VALUE_NOOP(location, type) \
template<> \
result<type> reader_rapl::impl::value<loc::location>(const sample&, uint8_t) const \
{ \
    return error(error_code::NOT_IMPL); \
}

#define SPECIALIZE_EVENT_IDX_NOOP(location) \
template<> \
int32_t reader_rapl::impl::event_idx<loc::location>(uint8_t) const \
{ \
    return -1; \
}

#define SPECIALIZE_VALUE_NOOP_ALL_LOCS(type) \
    SPECIALIZE_VALUE_NOOP(sys, type) \
    SPECIALIZE_VALUE_NOOP(pkg, type) \
    SPECIALIZE_VALUE_NOOP(cores, type) \
    SPECIALIZE_VALUE_NOOP(uncore, type) \
    SPECIALIZE_VALUE_NOOP(nest, type) \
    SPECIALIZE_VALUE_NOOP(mem, type) \
    SPECIALIZE_VALUE_NOOP(gpu, type)

#if defined NRG_X86_64

// SPECIALIZE_EVENT_IDX_NOOP(sys)
// SPECIALIZE_EVENT_IDX_NOOP(nest)
// SPECIALIZE_EVENT_IDX_NOOP(gpu)
// SPECIALIZE_VALUE_NOOP(sys, units_energy)
// SPECIALIZE_VALUE_NOOP(nest, units_energy)
// SPECIALIZE_VALUE_NOOP(gpu, units_energy)
// SPECIALIZE_VALUE_NOOP_ALL_LOCS(units_power)
// SPECIALIZE_VALUE_NOOP_ALL_LOCS(units_time)

#elif defined NRG_PPC64

// SPECIALIZE_EVENT_IDX_NOOP(uncore)
// SPECIALIZE_VALUE_NOOP(uncore, units_power)
// SPECIALIZE_VALUE_NOOP(uncore, units_time)
// SPECIALIZE_VALUE_NOOP_ALL_LOCS(units_energy)

#endif // defined NRG_X86_64
#endif // CPU_NONE

// explicit instantiation

#define INSTANTIATE_EVENT_IDX(location) \
    template \
    int32_t reader_rapl::event_idx<loc::location>(uint8_t skt) const

#define INSTANTIATE_VALUE(location, type) \
    template \
    result<type> \
    reader_rapl::value<loc::location>(const sample& s, uint8_t skt) const

#define INSTANTIATE_VALUES(location, type) \
    template \
    std::vector<std::pair<uint32_t, type>> \
    reader_rapl::values<loc::location>(const sample& s) const

#if defined NRG_X86_64

#define INSTANTIATE_TYPES(macro, location) \
    macro(location, units_energy)

#define INSTANTIATE_ALL(macro) \
    macro(pkg); \
    macro(cores); \
    macro(uncore); \
    macro(mem)

#elif defined NRG_PPC64

#define INSTANTIATE_TYPES(macro, location) \
    macro(location, units_power); \
    macro(location, units_time)

#define INSTANTIATE_ALL(macro) \
    macro(sys); \
    macro(pkg); \
    macro(cores); \
    macro(nest); \
    macro(mem); \
    macro(gpu)

#endif

#define INSTANTIATE_VALUE_ALL(location) \
    INSTANTIATE_TYPES(INSTANTIATE_VALUE, location)

#define INSTANTIATE_VALUES_ALL(location) \
    INSTANTIATE_TYPES(INSTANTIATE_VALUES, location)

INSTANTIATE_ALL(INSTANTIATE_EVENT_IDX);
INSTANTIATE_ALL(INSTANTIATE_VALUE_ALL);
INSTANTIATE_ALL(INSTANTIATE_VALUES_ALL);
