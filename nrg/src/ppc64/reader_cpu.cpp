#include "../fileline.hpp"
#include "../common/cpu/funcs.hpp"
#include "reader_cpu.hpp"

#include <nrg/location.hpp>
#include <nrg/sample.hpp>

#include <nonstd/expected.hpp>
#include <util/concat.hpp>

#include <cassert>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

#if !defined(NRG_OCC_USE_DUMMY_FILE)
#define NRG_OCC_USE_DUMMY_FILE "/sys/firmware/opal/exports/occ_inband_sensors"
#endif

// Specification reference
// https://github.com/open-power/docs/blob/master/occ/OCC_P9_FW_Interfaces.pdf

namespace
{
    namespace occ
    {
        // inject these types into the occ namespace
        using ::nrgprf::sensor_type;
        using ::nrgprf::sensor_loc;
        using ::nrgprf::sensor_buffers;
        using ::nrgprf::sensor_names_entry;

        constexpr char sensors_file[] = NRG_OCC_USE_DUMMY_FILE;

        constexpr size_t sensor_buffer_gap = 4096; // 4 kB
        constexpr size_t sensor_readings_size = 40 * 1024; // 40 kB
        constexpr size_t sensor_ping_buffer_offset = 0xdc00;
        constexpr size_t sensor_ping_buffer_size = sensor_readings_size;
        constexpr size_t sensor_pong_buffer_offset = 0x18c00;
        constexpr size_t sensor_pong_buffer_size = sensor_ping_buffer_size;

        constexpr size_t max_count = 8;
        constexpr size_t bar2_offset = 0x580000;
        constexpr size_t sensor_data_block_size = 150 * 1024; // 150 kB

        constexpr size_t sensor_header_version = 1;
        constexpr size_t sensor_data_header_block_offset = 0;
        constexpr size_t sensor_data_header_block_size = 1024; // 1 kB
        constexpr size_t sensor_names_offset = 0x400;
        constexpr size_t sensor_names_size = 50 * 1024; // 50 kB

        // TODO: Need to confirm whether sensor GSIDs are constant or dynamically assigned
        // (at reboot, for example)
        constexpr uint16_t gsid_pwrsys = 20;
        constexpr uint16_t gsid_pwrgpu = 24;
        constexpr uint16_t gsid_pwrproc = 48;
        constexpr uint16_t gsid_pwrmem = 49;
        constexpr uint16_t gsid_pwrvdd = 56;
        constexpr uint16_t gsid_pwrvdn = 57;

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

        struct sensor_structure_common
        {
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
            constexpr static size_t size = sensor_readings_size;
            constexpr static size_t pad = 8; // valid byte + 7 bytes reserved
            uint8_t valid;
            uint8_t __reserved[7];
            uint8_t readings[size - pad];
        } __attribute__((__packed__));
    }
}

namespace nrgprf::loc
{
    struct pkg : std::integral_constant<int, bitnum(locmask::pkg)> {};
    struct cores : std::integral_constant<int, bitnum(locmask::cores)> {};
    struct uncore : std::integral_constant<int, bitnum(locmask::uncore)> {};
    struct mem : std::integral_constant<int, bitnum(locmask::mem)> {};
    struct sys : std::integral_constant<int, bitnum(locmask::sys)> {};
    struct gpu : std::integral_constant<int, bitnum(locmask::gpu)> {};
}

namespace nrgprf
{
    static std::istream& operator>>(std::istream&, sensor_buffers&);
    static std::istream& operator>>(std::istream&, sensor_names_entry&);
    static std::ostream& operator<<(std::ostream&, sensor_type);
    static std::ostream& operator<<(std::ostream&, sensor_loc);
    static std::ostream& operator<<(std::ostream&, const sensor_names_entry&);

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

    struct sensor_buffers
    {
        constexpr static size_t size = occ::sensor_pong_buffer_offset -
            occ::sensor_ping_buffer_offset + occ::sensor_pong_buffer_size;

        static_assert(size == occ::sensor_ping_buffer_size +
            occ::sensor_pong_buffer_size +
            occ::sensor_buffer_gap);

        occ::sensor_readings_buffer ping;
        uint8_t __reserved[occ::sensor_buffer_gap];
        occ::sensor_readings_buffer pong;
    } __attribute__((__packed__));
}

namespace
{
    namespace occ
    {
        static_assert(24 == sensor_data_header_block::size,
            "occ::sensor_data_header_block::size != 24");
        static_assert(48 == sensor_names_entry::size,
            "occ::sensor_names_entry::size != 48");
        static_assert(48 == sensor_structure_v1::size,
            "occ::sensor_structure_v1::size != 48");
        static_assert(24 == sensor_structure_v2::size,
            "occ::sensor_structure_v2::size != 24");
        static_assert(sizeof(sensor_readings_buffer) == sensor_readings_buffer::size,
            "occ::sensor_readings_buffer::size != 40960");
        static_assert(sizeof(sensor_buffers) == sensor_buffers::size,
            "occ::sensor_buffers::size != 86016");

        namespace detail
        {
            template<typename T>
            std::string to_hex_string(const T& arg)
            {
                std::ostringstream ss;
                ss << std::hex << arg;
                return ss.str();
            }

            double to_double(uint32_t val)
            {
                return (val >> 8) * std::pow(10, static_cast<int8_t>(val & 0xff));
            }

            template<typename T, size_t sz = sizeof(T)>
            size_t retrieve_field(T* into, const void* from)
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
            size_t retrieve_field(sensor_type* into, const void* from)
            {
                std::memcpy(into, from, sz);
                *into = static_cast<sensor_type>(
                    be16toh(static_cast<std::underlying_type_t<sensor_type>>(*into)));
                return sz;
            }

            template<size_t sz = sizeof(sensor_loc)>
            size_t retrieve_field(sensor_loc* into, const void* from)
            {
                std::memcpy(into, from, sz);
                *into = static_cast<sensor_loc>(
                    be16toh(static_cast<std::underlying_type_t<sensor_loc>>(*into)));
                return sz;
            }

            template<size_t sz>
            size_t retrieve_field(char* into, const void* from)
            {
                if constexpr (sz > 1)
                    std::memcpy(into, from, sz);
                else
                    *into = *static_cast<const char*>(from);
                return sz;
            }
        }

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
                result = sstruct.sv2.gsid == nentry.gsid;
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

        bool get_sensor_record(
            const sensor_buffers& buffs,
            const sensor_names_entry& entry,
            sensor_structure_v1_sample& out)
        {
            assert(buffs.ping.valid || buffs.pong.valid);
            if (!buffs.ping.valid && !buffs.pong.valid)
                return false;

            if (buffs.ping.valid && buffs.pong.valid)
            {
                auto ping_ts = get_timestamp(buffs.ping, entry.reading_offset);
                auto pong_ts = get_timestamp(buffs.pong, entry.reading_offset);
                if (ping_ts > pong_ts)
                    out = get_v1_sample(buffs.ping, entry.reading_offset);
                else
                    out = get_v1_sample(buffs.pong, entry.reading_offset);
            }
            if (buffs.pong.valid)
                out = get_v1_sample(buffs.pong, entry.reading_offset);
            if (buffs.ping.valid)
                out = get_v1_sample(buffs.ping, entry.reading_offset);
            return true;
        }

    #if defined NRG_OCC_DEBUG_PRINTS
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

        void debug_print(const sensor_data_header_block& header)
        {
            std::cout << header << std::endl;
        }

        bool debug_print(const sensor_names_entry& entry, const sensor_structure& record)
        {
            std::cout << entry << "\n";
            switch (entry.structure_version)
            {
            case 1:
                std::cout << "  " << record.sv1 << "\n";
                return true;
            case 2:
                std::cout << "  " << record.sv2 << "\n";
                return true;
            default:
                assert(false);
                return false;
            }
        }
    #else // !defined NRG_OCC_DEBUG_PRINTS
        void debug_print(const sensor_data_header_block&)
        {}

        bool debug_print(const sensor_names_entry&, const sensor_structure&)
        {
            return true;
        }
    #endif // defined NRG_OCC_DEBUG_PRINTS
    }

    struct sensor_static_data
    {
        uint16_t gsid;
        occ::sensor_type type;
        occ::sensor_loc loc;
    };

    constexpr sensor_static_data bit_to_sensor_data[] =
    {
        { occ::gsid_pwrproc, occ::sensor_type::power, occ::sensor_loc::proc },
        { occ::gsid_pwrvdd,  occ::sensor_type::power, occ::sensor_loc::proc },
        { occ::gsid_pwrvdn,  occ::sensor_type::power, occ::sensor_loc::proc },
        { occ::gsid_pwrmem,  occ::sensor_type::power, occ::sensor_loc::memory },
        { occ::gsid_pwrsys,  occ::sensor_type::power, occ::sensor_loc::system },
        { occ::gsid_pwrgpu,  occ::sensor_type::power, occ::sensor_loc::gpu }
    };

    constexpr int32_t sensor_gsid_to_index(uint16_t gsid)
    {
        using namespace nrgprf;
        switch (gsid)
        {
        case occ::gsid_pwrsys:
            return loc::sys::value;
        case occ::gsid_pwrgpu:
            return loc::gpu::value;
        case occ::gsid_pwrproc:
            return loc::pkg::value;
        case occ::gsid_pwrmem:
            return loc::mem::value;
        case occ::gsid_pwrvdd:
            return loc::cores::value;
        case occ::gsid_pwrvdn:
            return loc::uncore::value;
        default:
            assert(false);
            return -1;
        }
    }

    template<typename T>
    constexpr uint16_t to_sensor_gsid()
    {
        using namespace nrgprf;
        static_assert(std::is_same_v<T, loc::sys> ||
            std::is_same_v<T, loc::pkg> ||
            std::is_same_v<T, loc::cores> ||
            std::is_same_v<T, loc::uncore> ||
            std::is_same_v<T, loc::mem> ||
            std::is_same_v<T, loc::gpu>,
            "T must be of type sys, pkg, cores, uncore, mem or gpu");
        if constexpr (std::is_same_v<T, loc::sys>)
            return occ::gsid_pwrsys;
        if constexpr (std::is_same_v<T, loc::gpu>)
            return occ::gsid_pwrgpu;
        if constexpr (std::is_same_v<T, loc::pkg>)
            return occ::gsid_pwrproc;
        if constexpr (std::is_same_v<T, loc::cores>)
            return occ::gsid_pwrvdd;
        if constexpr (std::is_same_v<T, loc::uncore>)
            return occ::gsid_pwrvdn;
        if constexpr (std::is_same_v<T, loc::mem>)
            return occ::gsid_pwrmem;
    }

    nrgprf::watts<double> canonicalize_power(uint16_t value, const occ::sensor_names_entry& entry)
    {
        if (std::string_view(entry.units) == "W")
            return nrgprf::watts<double>(value * entry.scaling_factor);
        else
        {
            assert(false);
            return nrgprf::watts<double>{};
        }
    }

    // OCC timestamps have a resolution of 512 MHz
    // this means that each value incremented in the counter corresponds to 1000/512 ns
    nrgprf::sensor_value::time_point canonicalize_timestamp(uint64_t timestamp)
    {
        using nrgprf::sensor_value;
        std::chrono::duration<uint64_t, std::ratio<1, 512000000UL>> dur(timestamp);
        return sensor_value::time_point(
            std::chrono::duration_cast<sensor_value::time_point::duration>(dur));
    }

    nrgprf::error get_sensor_buffers(std::ifstream& ifs,
        uint32_t occ_num,
        occ::sensor_buffers& buffs)
    {
        using namespace nrgprf;
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

    nrgprf::error get_header(std::ifstream& ifs,
        uint32_t occ_num,
        occ::sensor_data_header_block& hb)
    {
        using namespace nrgprf;
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

        debug_print(hb);
        return error::success();
    }

    nrgprf::error get_names_entries(std::ifstream& ifs,
        uint32_t occ_num,
        std::vector<occ::sensor_names_entry>& entries)
    {
        using namespace nrgprf;
        assert(occ_num < occ::max_count);
        size_t occ_offset = occ_num * occ::sensor_data_block_size;
        std::string occ_num_str = std::to_string(occ_num);

        ifs.seekg(occ_offset + occ::sensor_names_offset);
        if (!ifs)
            return { error_code::SYSTEM, system_error_str(
                cmmn::concat("Error seeking to OCC ", occ_num_str, " sensor names"))
        };
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

    nrgprf::error get_sensor_structs(const occ::sensor_buffers& buffs,
        const std::vector<occ::sensor_names_entry>& entries,
        std::vector<occ::sensor_structure>& structs)
    {
        using namespace nrgprf;
        for (const auto& entry : entries)
        {
            occ::sensor_structure sstruct;
            if (!occ::get_sensor_record(buffs, entry, sstruct))
                return { error_code::FORMAT_ERROR, "Both ping and pong buffers are not valid" };
            if (std::string msg; !occ::assert_sensor_structure(sstruct, entry, msg))
                return { error_code::FORMAT_ERROR, std::move(msg) };
            if (!debug_print(entry, sstruct))
                return { error_code::FORMAT_ERROR, "Unsupported sensor structure version found" };
            structs.push_back(sstruct);
        }
        return error::success();
    }
}

namespace nrgprf
{
    std::istream& operator>>(std::istream& is, sensor_buffers& buffs)
    {
        return is.read(reinterpret_cast<std::istream::char_type*>(&buffs), sizeof(buffs));
    }

    std::istream& operator>>(std::istream& is, sensor_names_entry& entry)
    {
        using namespace occ::detail;
        std::istream::char_type buffer[occ::sensor_names_entry::size];
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

    std::ostream& operator<<(std::ostream& os, sensor_type type)
    {
        using occ::sensor_type;
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
        using occ::sensor_loc;
        using cast_type = std::underlying_type_t<sensor_loc>;
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

    reader_impl::reader_impl(
        location_mask lmask,
        socket_mask smask,
        error& err,
        std::ostream& os)
        :
        _file(std::make_shared<std::ifstream>(occ::sensors_file, std::ios::in | std::ios::binary)),
        _event_map(),
        _active_events()
    {
        if (!*_file)
        {
            err = { error_code::SYSTEM, system_error_str(
                cmmn::concat("Error opening ", occ::sensors_file)) };
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
        os << fileline(cmmn::concat("Found ", std::to_string(*sockets), " sockets\n"));
        for (uint32_t occ_num = 0; occ_num < *sockets; occ_num++)
        {
            if (!smask[occ_num])
                continue;
            os << fileline(cmmn::concat("Registered socket: ", std::to_string(occ_num), "\n"));

            occ::sensor_data_header_block hb{};
            if (err = get_header(*_file, occ_num, hb))
                return;

            std::vector<occ::sensor_names_entry> entries(hb.sensor_count, occ::sensor_names_entry{});
            if (err = get_names_entries(*_file, occ_num, entries))
                return;

            occ::sensor_buffers sbuffs{};
            if (err = get_sensor_buffers(*_file, occ_num, sbuffs))
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
                if (occ_num != 0 && bit_to_sensor_data[loc].gsid == occ::gsid_pwrsys)
                    continue;
                add_event(entries, occ_num, loc, os);
            }
        }

        if (!num_events())
            err = { error_code::SETUP_ERROR, "No events were added" };
    }

    error reader_impl::add_event(
        const std::vector<occ::sensor_names_entry>& entries,
        uint32_t occ_num,
        uint32_t loc,
        std::ostream& os)
    {
        int8_t& idxref = _event_map[occ_num][loc];
        // find if an event for a certain OCC has been added
        for (auto it = _active_events.cbegin(); it != _active_events.end(); it++)
            if (it->occ_num == occ_num)
                idxref = std::distance(_active_events.cbegin(), it);

        // if it has then the index must be >= 0
        // in this case, push back a new event and register its index
        if (idxref < 0)
        {
            idxref = _active_events.size();
            _active_events.push_back({ occ_num, std::vector<occ::sensor_names_entry>() });
        }

        for (const auto& entry : entries)
        {
            const auto& sensor_data = bit_to_sensor_data[loc];
            auto& active_entries = _active_events[idxref].entries;
            if (entry.gsid == sensor_data.gsid &&
                entry.type == sensor_data.type &&
                entry.location == sensor_data.loc)
            {
                assert(entry.structure_version == 1);
                if (entry.structure_version != 1)
                    return { error_code::NOT_IMPL, "Unsupported structure version" };
                active_entries.push_back(entry);
                os << fileline("added event - idx=") << +idxref
                    << " OCC=" << occ_num << " " << entry << "\n";
            }
        }
        return error::success();
    }

    error reader_impl::read_single_occ(const event_data& ed,
        sensor_buffers& sbuffs,
        sample& s) const
    {
        if (error err = get_sensor_buffers(*_file, ed.occ_num, sbuffs))
            return err;
        for (const auto& entry : ed.entries)
        {
            size_t stride = ed.occ_num * nrgprf::max_domains +
                sensor_gsid_to_index(entry.gsid);

            occ::sensor_structure_v1_sample record;
            if (!get_sensor_record(sbuffs, entry, record))
                return { error_code::READ_ERROR, "Both ping and pong buffers are not valid" };

            s.data.timestamps[stride] = record.timestamp;
            s.data.cpu[stride] = record.sample;
        }
        return error::success();
    }

    error reader_impl::read(sample& s) const
    {
        occ::sensor_buffers sbuffs;
        for (const auto& ed : _active_events)
            if (error err = read_single_occ(ed, sbuffs, s))
                return err;
        return error::success();
    }

    // Since sensors are read in bulk, reading with an index reads all sensors in some OCC
    error reader_impl::read(sample& s, uint8_t idx) const
    {
        occ::sensor_buffers sbuffs;
        if (error err = read_single_occ(_active_events[idx], sbuffs, s))
            return err;
        return error::success();
    }

    size_t reader_impl::num_events() const
    {
        size_t num_events = 0;
        for (const auto& ed : _active_events)
            num_events += ed.entries.size();
        return num_events;
    }

    template<typename Location>
    int32_t reader_impl::event_idx(uint8_t skt) const
    {
        return _event_map[skt][Location::value];
    }

    template<typename Location>
    result<sensor_value> reader_impl::value(const sample& s, uint8_t skt) const
    {
        assert(skt < max_sockets);
        using rettype = result<sensor_value>;
        int32_t idx = event_idx<Location>(skt);
        if (idx < 0)
            return rettype(nonstd::unexpect, error_code::NO_EVENT);
        uint32_t stride = skt * nrgprf::max_domains + Location::value;

        auto value_timestamp = s.data.timestamps[stride];
        auto value_sample = s.data.cpu[stride];
        if (!value_timestamp || !value_sample)
            return rettype(nonstd::unexpect, error_code::NO_EVENT);
        for (const auto& sensor_entry : _active_events[idx].entries)
        {
            if (sensor_entry.gsid == to_sensor_gsid<Location>())
            {
                watts<double> power = canonicalize_power(value_sample, sensor_entry);
                sensor_value::time_point tp = canonicalize_timestamp(value_timestamp);
                if (!power.count())
                    return rettype(nonstd::unexpect,
                        error_code::NOT_IMPL, "Unsupported power units found");
                return sensor_value{ tp, unit_cast<decltype(sensor_value::power)>(power) };
            }
        };
        return rettype(nonstd::unexpect, error_code::NO_EVENT);
    }
}

#include "../instantiate.hpp"
INSTANTIATE_ALL(nrgprf::reader_impl, INSTANTIATE_EVENT_IDX);
INSTANTIATE_ALL(nrgprf::reader_impl, INSTANTIATE_VALUE);
