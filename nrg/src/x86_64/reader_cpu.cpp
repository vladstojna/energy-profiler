#include "../util.hpp"
#include "../common/cpu/funcs.hpp"
#include "reader_cpu.hpp"

#include <nrg/location.hpp>
#include <nrg/sample.hpp>

#include <nonstd/expected.hpp>
#include <util/concat.hpp>

#include <charconv>
#include <cstring>
#include <iostream>

#include <fcntl.h>
#include <unistd.h>

namespace nrgprf::loc
{
    struct pkg : std::integral_constant<int, bitnum(locmask::pkg)> {};
    struct cores : std::integral_constant<int, bitnum(locmask::cores)> {};
    struct uncore : std::integral_constant<int, bitnum(locmask::uncore)> {};
    struct mem : std::integral_constant<int, bitnum(locmask::mem)> {};
    struct sys {};
    struct gpu {};
}

namespace
{
    constexpr char EVENT_PKG_PREFIX[] = "package";
    constexpr char EVENT_PP0[] = "core";
    constexpr char EVENT_PP1[] = "uncore";
    constexpr char EVENT_DRAM[] = "dram";

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

    bool is_package_domain(const char* name)
    {
        return !std::strncmp(EVENT_PKG_PREFIX, name, sizeof(EVENT_PKG_PREFIX) - 1);
    }

    int32_t domain_index_from_name(const char* name)
    {
        if (is_package_domain(name))
            return nrgprf::loc::pkg::value;
        if (!std::strncmp(EVENT_PP0, name, sizeof(EVENT_PP0) - 1))
            return nrgprf::loc::cores::value;
        if (!std::strncmp(EVENT_PP1, name, sizeof(EVENT_PP1) - 1))
            return nrgprf::loc::uncore::value;
        if (!std::strncmp(EVENT_DRAM, name, sizeof(EVENT_DRAM) - 1))
            return nrgprf::loc::mem::value;
        return -1;
    }

    nrgprf::result<int32_t> get_domain_idx(const char* base)
    {
        using namespace nrgprf;
        // open the */name file, read the name and obtain the domain index
        using rettype = result<int32_t>;
        char name[64];
        char filename[256];
        snprintf(filename, sizeof(filename), "%s/name", base);
        auto filed = file_descriptor::create(filename);
        if (!filed)
            return rettype(nonstd::unexpect, std::move(filed.error()));
        if (read_buff(filed->value, name, sizeof(name)) < 0)
            return rettype(nonstd::unexpect,
                error_code::SYSTEM,
                system_error_str(filename));
        int32_t didx = domain_index_from_name(name);
        if (didx < 0)
            return rettype(nonstd::unexpect,
                error_code::INVALID_DOMAIN_NAME,
                cmmn::concat("invalid domain name: ", name));
        return didx;
    }

    nrgprf::result<uint32_t> get_package_number(const char* base)
    {
        using namespace nrgprf;
        using rettype = decltype(get_package_number(nullptr));
        char name[64];
        char filename[256];
        // read the <domain>/name content
        snprintf(filename, sizeof(filename), "%s/name", base);
        auto filed = file_descriptor::create(filename);
        if (!filed)
            return rettype(nonstd::unexpect, std::move(filed.error()));
        ssize_t namelen = read_buff(filed->value, name, sizeof(name));
        if (namelen < 0)
            return rettype(nonstd::unexpect, error_code::SYSTEM,
                system_error_str(filename));
        // check whether the contents follow the package-<number> pattern
        if (!is_package_domain(name))
            return rettype(nonstd::unexpect, error_code::SETUP_ERROR,
                "Attempted retrieval of the package number on a non-package domain");
        // offset package- to point to the package number;
        // null-terminator counts as the dash
        const char* pkg_num_start = name + sizeof(EVENT_PKG_PREFIX);
        uint32_t pkg_num;
        auto [p, ec] = std::from_chars(pkg_num_start, name + namelen, pkg_num, 10);
        if (auto code = std::make_error_code(ec))
            return rettype(nonstd::unexpect, error_code::SETUP_ERROR,
                cmmn::concat("Error reading the package number ", code.message()));
        // package numbers start at 0, so the maximum is max_sockets - 1
        if (pkg_num >= max_sockets)
            return rettype(nonstd::unexpect, error_code::TOO_MANY_SOCKETS, cmmn::concat(
                "Package number greater than maximum number of supported sockets, got ",
                std::to_string(pkg_num)));
        return pkg_num;
    }

    nrgprf::result<nrgprf::event_data> get_event_data(const char* base)
    {
        // open the */max_energy_range_uj file and save the max value
        // open the */energy_uj file and save the file descriptor
        using namespace nrgprf;
        using rettype = result<event_data>;
        char filename[256];
        snprintf(filename, sizeof(filename), "%s/max_energy_range_uj", base);
        auto filed = file_descriptor::create(filename);
        if (!filed)
            return rettype(nonstd::unexpect, std::move(filed.error()));
        uint64_t max_value;
        if (read_uint64(filed->value, &max_value) < 0)
            return rettype(nonstd::unexpect,
                error_code::SYSTEM,
                system_error_str(filename));
        snprintf(filename, sizeof(filename), "%s/energy_uj", base);
        filed = file_descriptor::create(filename);
        if (!filed)
            return rettype(nonstd::unexpect, std::move(filed.error()));
        return event_data{ std::move(*filed), max_value };
    }

    bool file_exists(std::string_view path)
    {
        return !access(path.data(), F_OK);
    }
}

namespace nrgprf
{
    result<file_descriptor> file_descriptor::create(const char* file)
    {
        nrgprf::error err = error::success();
        file_descriptor fd(file, err);
        if (err)
            return result<file_descriptor>(nonstd::unexpect, std::move(err));
        return fd;
    }

    file_descriptor::file_descriptor(const char* file, error& err) :
        value(open(file, O_RDONLY))
    {
        if (value == -1)
            err = { error_code::SYSTEM, system_error_str(file) };
    }

    file_descriptor::file_descriptor(const file_descriptor& other) :
        value(dup(other.value))
    {
        if (value == -1)
            throw std::runtime_error("file_descriptor: error duplicating file descriptor");
    }

    file_descriptor::file_descriptor(file_descriptor&& other) noexcept :
        value(std::exchange(other.value, -1))
    {}

    file_descriptor::~file_descriptor() noexcept
    {
        if (value >= 0 && close(value) == -1)
            perror("file_descriptor: error closing file");
    }

    file_descriptor& file_descriptor::operator=(file_descriptor&& other) noexcept
    {
        value = other.value;
        other.value = -1;
        return *this;
    }

    event_data::event_data(file_descriptor&& fd, uint64_t max) :
        fd(std::move(fd)),
        max(max),
        prev(0),
        curr_max(0)
    {}

    reader_impl::reader_impl(
        location_mask dmask,
        socket_mask skt_mask,
        error& ec,
        std::ostream& os) :
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
        os << fileline(cmmn::concat("found ", std::to_string(*num_skts), " sockets\n"));
        for (uint8_t skt = 0; skt < *num_skts; skt++)
        {
            char base[96];
            int written = snprintf(base, sizeof(base),
                "/sys/class/powercap/intel-rapl/intel-rapl:%u", skt);
            // if domain does not exist, no need to consider the remaining domains
            if (!file_exists(base))
                continue;
            result<uint32_t> package_num = get_package_number(base);
            if (!package_num && (ec = std::move(package_num.error())))
                return;
            if (!skt_mask[*package_num])
                continue;
            os << fileline(cmmn::concat("registered socket: ", std::to_string(*package_num), "\n"));
            if (ec = add_event(base, dmask, *package_num, os))
                return;
            // already found one domain above
            for (uint8_t domain_count = 0; domain_count < max_domains - 1; domain_count++)
            {
                snprintf(base + written, sizeof(base) - written,
                    "/intel-rapl:%u:%u", skt, domain_count);
                // only consider the domain if the file exists
                if (file_exists(base) && (ec = add_event(base, dmask, *package_num, os)))
                    return;
            }
        }
        if (!num_events())
            ec = { error_code::SETUP_ERROR, "No events were added" };
    }

    error reader_impl::read(sample& s) const
    {
        for (size_t ix = 0; ix < _active_events.size(); ix++)
        {
            error err = read(s, ix);
            if (err)
                return err;
        };
        return error::success();
    }

    error reader_impl::read(sample& s, uint8_t ev_idx) const
    {
        uint64_t curr;
        if (read_uint64(_active_events[ev_idx].fd.value, &curr) == -1)
            return { error_code::SYSTEM, system_error_str("Error reading counters") };
        if (curr < _active_events[ev_idx].prev)
        {
            std::cerr << fileline("detected wraparound\n");
            _active_events[ev_idx].curr_max += _active_events[ev_idx].max;
        }
        _active_events[ev_idx].prev = curr;
        s.data.cpu[ev_idx] = curr + _active_events[ev_idx].curr_max;
        return error::success();
    }

    size_t reader_impl::num_events() const
    {
        return _active_events.size();
    }

    template<typename Location>
    int32_t reader_impl::event_idx(uint8_t skt) const
    {
        return _event_map[skt][Location::value];
    }

    template<>
    int32_t reader_impl::event_idx<loc::sys>(uint8_t) const
    {
        return -1;
    }

    template<>
    int32_t reader_impl::event_idx<loc::gpu>(uint8_t) const
    {
        return -1;
    }

    template<typename Location>
    result<sensor_value> reader_impl::value(const sample& s, uint8_t skt) const
    {
        using rettype = result<sensor_value>;
        if (event_idx<Location>(skt) < 0)
            return rettype(nonstd::unexpect, error_code::NO_EVENT);
        auto res = s.data.cpu[event_idx<Location>(skt)];
        if (!res)
            return rettype(nonstd::unexpect, error_code::NO_EVENT);
        return sensor_value{ res };
    }

    template<>
    result<sensor_value> reader_impl::value<loc::sys>(const sample&, uint8_t) const
    {
        return result<sensor_value>(nonstd::unexpect, error_code::NO_EVENT);
    }

    template<>
    result<sensor_value> reader_impl::value<loc::gpu>(const sample&, uint8_t) const
    {
        return result<sensor_value>(nonstd::unexpect, error_code::NO_EVENT);
    }

    error reader_impl::add_event(
        const char* base, location_mask dmask, uint8_t skt, std::ostream& os)
    {
        result<int32_t> didx = get_domain_idx(base);
        if (!didx)
            return std::move(didx.error());
        if (dmask[*didx])
        {
            result<event_data> event_data = get_event_data(base);
            if (!event_data)
                return std::move(event_data.error());
            os << fileline(cmmn::concat("added event: ", base, "\n"));
            _event_map[skt][*didx] = _active_events.size();
            _active_events.push_back(std::move(*event_data));
        }
        return error::success();
    }
}

#include "../instantiate.hpp"
INSTANTIATE_ALL(nrgprf::reader_impl, INSTANTIATE_EVENT_IDX);
INSTANTIATE_ALL(nrgprf::reader_impl, INSTANTIATE_VALUE);
