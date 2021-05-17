// reader_rapl.cpp

#include <nrg/error.hpp>
#include <nrg/reader_rapl.hpp>
#include <nrg/sample.hpp>

#include <cassert>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <iostream>

#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

using namespace nrgprf;

constexpr static const char EVENT_PKG_PREFIX[] = "package";
constexpr static const char EVENT_PP0[] = "core";
constexpr static const char EVENT_PP1[] = "uncore";
constexpr static const char EVENT_DRAM[] = "dram";

constexpr static const uint8_t EVENT_PKG_IDX = 0;
constexpr static const uint8_t EVENT_PP0_IDX = 1;
constexpr static const uint8_t EVENT_PP1_IDX = 2;
constexpr static const uint8_t EVENT_DRAM_IDX = 3;

// structs


struct domain_index
{
    rapl_domain domain;
    int8_t index;

    domain_index(rapl_domain d, int8_t idx) :
        domain(d), index(idx)
    {}

    operator bool() const
    {
        return domain != rapl_domain::NONE && index >= 0;
    }
};


// begin helper functions


std::string system_error_str(const char* prefix)
{
    char buffer[256];
    return std::string(prefix)
        .append(": ")
        .append(strerror_r(errno, buffer, 256));
}


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


result<uint8_t> count_sockets()
{
    char filename[128];
    bool pkg_found[max_sockets]{ false };
    uint8_t ret = 0;
    for (int i = 0; ; i++)
    {
        uint64_t pkg;
        snprintf(filename, sizeof(filename),
            "/sys/devices/system/cpu/cpu%d/topology/physical_package_id", i);
        if (access(filename, F_OK) == -1)
            break;
        result<detail::file_descriptor> filed = detail::file_descriptor::create(filename);
        if (!filed)
            return std::move(filed.error());
        if (read_uint64(filed.value().value, &pkg) < 0)
            return error(error_code::SYSTEM, system_error_str(filename));
        if (pkg >= max_sockets)
            return error(error_code::TOO_MANY_SOCKETS,
                "Too many sockets (a maximum of 8 is supported)");
        if (!pkg_found[pkg])
        {
            pkg_found[pkg] = true;
            ret++;
        }
    };
    if (ret == 0)
        return error(error_code::NO_SOCKETS, "no sockets found");
    return ret;
}


domain_index domain_index_from_name(const char* name)
{
    if (!strncmp(EVENT_PKG_PREFIX, name, sizeof(EVENT_PKG_PREFIX) - 1))
        return { rapl_domain::PKG, EVENT_PKG_IDX };
    if (!strncmp(EVENT_PP0, name, sizeof(EVENT_PP0) - 1))
        return { rapl_domain::PP0, EVENT_PP0_IDX };
    if (!strncmp(EVENT_PP1, name, sizeof(EVENT_PP1) - 1))
        return { rapl_domain::PP1, EVENT_PP1_IDX };
    if (!strncmp(EVENT_DRAM, name, sizeof(EVENT_DRAM) - 1))
        return { rapl_domain::DRAM, EVENT_DRAM_IDX };
    return { rapl_domain::NONE, -1 };
}


result<units_energy> get_value(const sample& s,
    const int8_t(&map)[max_sockets][rapl_domains], uint8_t skt, uint8_t idx)
{
    if (map[skt][idx] < 0)
        return error(error_code::NO_EVENT);
    result<sample::value_type> result = s.at_cpu(idx);
    if (!result)
        return std::move(result.error());
    return result.value();
}


result<domain_index> get_domain_idx(const char* base)
{
    // open the */name file, read the name and obtain the domain and index

    char name[64];
    char filename[256];
    snprintf(filename, sizeof(filename), "%s/name", base);
    result<detail::file_descriptor> filed = detail::file_descriptor::create(filename);
    if (!filed)
        return std::move(filed.error());
    if (read_buff(filed.value().value, name, sizeof(name)) < 0)
        return error(error_code::SYSTEM, system_error_str(filename));
    domain_index didx = domain_index_from_name(name);
    if (!didx)
        return error(error_code::INVALID_DOMAIN_NAME,
            std::string("invalid domain name - ").append(name));
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
{};


detail::event_data::event_data(file_descriptor&& fd, uint64_t max) :
    fd(std::move(fd)),
    max(max),
    prev(0),
    curr_max(0)
{};


// reader_rapl


reader_rapl::reader_rapl(rapl_domain dmask, uint8_t skt_mask, error& ec) :
    _event_map(),
    _active_events()
{
    for (uint8_t skt = 0; skt < max_sockets; skt++)
        for (uint8_t ev = 0; ev < rapl_domains; ev++)
            _event_map[skt][ev] = -1;

    result<uint8_t> num_skts = count_sockets();
    if (!num_skts)
    {
        ec = std::move(num_skts.error());
        return;
    }
    std::cout << "found " << +num_skts.value() << " sockets\n";
    for (uint8_t skt = 0; skt < num_skts.value(); skt++)
    {
        if (!(skt_mask & (1 << skt)))
            continue;
        std::cout << "socket " << +skt << " not masked\n";

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

reader_rapl::reader_rapl(error& ec) :
    reader_rapl(rapl_domain::PKG | rapl_domain::PP0 | rapl_domain::PP1 | rapl_domain::DRAM, 0xff, ec)
{}

error reader_rapl::add_event(const char* base, rapl_domain dmask, uint8_t skt)
{
    result<domain_index> didx = get_domain_idx(base);
    if (!didx)
        return std::move(didx.error());
    if ((didx.value().domain & dmask) != rapl_domain::NONE)
    {
        result<detail::event_data> event_data = get_event_data(base);
        if (!event_data)
            return std::move(event_data.error());
        std::cout << "added event: " << base << "\n";
        _event_map[skt][didx.value().index] = _active_events.size();
        _active_events.push_back(std::move(event_data.value()));
    }
    return error::success();
}

error reader_rapl::read(sample& s) const
{
    for (size_t ix = 0; ix < _active_events.size(); ix++)
    {
        error err = read(s, ix);
        if (err)
            return err;
    };
    return error::success();
}

error reader_rapl::read(sample& s, uint8_t idx) const
{
    uint64_t curr;
    if (read_uint64(_active_events[idx].fd.value, &curr) == -1)
        return { error_code::SYSTEM, system_error_str("Error reading counters") };
    if (curr < _active_events[idx].prev)
    {
        std::cout << "reader_rapl: detected wraparound\n";
        _active_events[idx].curr_max += _active_events[idx].max;
    }
    _active_events[idx].prev = curr;
    s.at_cpu(idx) = curr + _active_events[idx].curr_max;
    return error::success();
}

int8_t reader_rapl::event_idx(rapl_domain domain, uint8_t skt) const
{
    switch (domain)
    {
    case rapl_domain::PKG:
        return _event_map[skt][EVENT_PKG_IDX];
    case rapl_domain::PP0:
        return _event_map[skt][EVENT_PP0_IDX];
    case rapl_domain::PP1:
        return _event_map[skt][EVENT_PP1_IDX];
    case rapl_domain::DRAM:
        return _event_map[skt][EVENT_DRAM_IDX];
    default:
        return -1;
    };
}

size_t reader_rapl::num_events() const
{
    return _active_events.size();
}

result<units_energy> reader_rapl::get_pkg_energy(const sample& s, uint8_t skt) const
{
    return get_value(s, _event_map, skt, EVENT_PKG_IDX);
}

result<units_energy> reader_rapl::get_pp0_energy(const sample& s, uint8_t skt) const
{
    return get_value(s, _event_map, skt, EVENT_PP0_IDX);
}

result<units_energy> reader_rapl::get_pp1_energy(const sample& s, uint8_t skt) const
{
    return get_value(s, _event_map, skt, EVENT_PP1_IDX);
}

result<units_energy> reader_rapl::get_dram_energy(const sample& s, uint8_t skt) const
{
    return get_value(s, _event_map, skt, EVENT_DRAM_IDX);
}
