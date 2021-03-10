// reader_rapl.cpp

#include "reader_rapl.hpp"
#include "sample.hpp"
#include "util.hpp"

#include <cassert>
#include <charconv>
#include <cstdio>

#include <papi.h>

using namespace nrgprf;

// begin helper functions

result<int> find_rapl_component()
{
    int numcmp = PAPI_num_components();
    int rapl_cid;
    int cid;
    for (cid = 0; cid < numcmp; cid++)
    {
        const PAPI_component_info_t* cmpinfo;
        if ((cmpinfo = PAPI_get_component_info(cid)) == NULL)
            return error(error_code::SETUP_ERROR, "PAPI_get_component_info failed");

        std::string_view name(cmpinfo->name, PAPI_MAX_STR_LEN);
        if (name.find("rapl") != name.npos)
        {
            rapl_cid = cid;
            printf("found rapl component at cid %d\n", rapl_cid);
            if (cmpinfo->disabled)
            {
                constexpr size_t sz = PAPI_MAX_STR_LEN + 32;
                char msg[sz];
                snprintf(msg, sz, "RAPL component disabled: %s", cmpinfo->disabled_reason);
                return error(error_code::SETUP_ERROR, msg);
            }
            return rapl_cid;
        }
    }
    return error(error_code::SETUP_ERROR, "no rapl component found"
        "configure PAPI with --with-component=\"rapl\"");
}

result<double> multiplier_from_units(const std::string_view& units)
{
    if (units.find("nJ") != units.npos)
        return 1e-9;
    return error(error_code::SETUP_ERROR, "Unknown unit");
}

result<rapl_domain> domain_from_event_name(const std::string_view& name, size_t& pos)
{
    static constexpr const char PKG_ENERGY[] = "PACKAGE_ENERGY:PACKAGE";
    static constexpr const char PP0_ENERGY[] = "PP0_ENERGY:PACKAGE";
    static constexpr const char PP1_ENERGY[] = "PP1_ENERGY:PACKAGE";
    static constexpr const char DRAM_ENERGY[] = "DRAM_ENERGY:PACKAGE";

    if ((pos = name.find(PKG_ENERGY)) != name.npos)
        return rapl_domain::PKG;
    if ((pos = name.find(PP0_ENERGY)) != name.npos)
        return rapl_domain::PP0;
    if ((pos = name.find(PP1_ENERGY)) != name.npos)
        return rapl_domain::PP1;
    if ((pos = name.find(DRAM_ENERGY)) != name.npos)
        return rapl_domain::DRAM;
    return error(error_code::SETUP_ERROR, "event does not represent energy readings");
}

uint8_t skt_from_event_name(const std::string_view& name, size_t pos)
{
    uint8_t skt = 0;
    auto [ptr, ec] = std::from_chars(&name.at(pos), &name.back(), skt, 10);
    assert(ec != std::errc());
    return skt;
}

bool skt_is_set(uint8_t skt_mask, uint8_t skt)
{
    return skt_mask & (1 << skt);
}

// end helper functions

reader_rapl::reader_rapl(rapl_domain dmask, uint8_t skt_mask, error& ec) :
    _evset(PAPI_NULL),
    _dmask(dmask),
    _skt_mask(skt_mask),
    _events()
{
    int retval;
    // initialize PAPI, if not initialized
    if (PAPI_is_initialized() == PAPI_NOT_INITED)
    {
        retval = PAPI_library_init(PAPI_VER_CURRENT);
        if (retval != PAPI_VER_CURRENT)
        {
            ec = { error_code::SETUP_ERROR, PAPI_strerror(retval) };
            return;
        }
    }
    // find the RAPL component
    result<int> rapl_cid = find_rapl_component();
    if (!rapl_cid)
    {
        ec = std::move(rapl_cid.error());
        return;
    }
    // create event set
    retval = PAPI_create_eventset(&_evset);
    if (retval != PAPI_OK)
    {
        ec = { error_code::SETUP_ERROR, PAPI_strerror(retval) };
        return;
    }
    // add the events
    error err = add_events(rapl_cid.value());
    if (err)
    {
        ec = std::move(err);
        return;
    }
    // start the counters
    retval = PAPI_start(_evset);
    if (retval != PAPI_OK)
    {
        ec = { error_code::SETUP_ERROR, PAPI_strerror(retval) };
        return;
    }
}

reader_rapl::~reader_rapl() noexcept
{
    if (PAPI_is_initialized() == PAPI_NOT_INITED)
        return;
    int retval = PAPI_stop(_evset, nullptr);
    if (retval != PAPI_OK)
        PAPI_perror(PAPI_strerror(retval));
    retval = PAPI_cleanup_eventset(_evset);
    if (retval != PAPI_OK)
        PAPI_perror(PAPI_strerror(retval));
    retval = PAPI_destroy_eventset(&_evset);
    if (retval != PAPI_OK)
        PAPI_perror(PAPI_strerror(retval));
    PAPI_shutdown();
}

error reader_rapl::add_events(int cid)
{
    struct event_cpu_tmp
    {
        rapl_domain domain = rapl_domain::NONE;
        uint8_t pkg, pp0, pp1, dram;
        double mult;
    };

    int code = PAPI_NATIVE_MASK;
    uint8_t event_idx = 0;
    event_cpu_tmp events[MAX_SOCKETS];

    for (int cmp_enum = PAPI_enum_cmp_event(&code, PAPI_ENUM_FIRST, cid);
        cmp_enum == PAPI_OK;
        cmp_enum = PAPI_enum_cmp_event(&code, PAPI_ENUM_EVENTS, cid))
    {
        PAPI_event_info_t evinfo;
        char event_name[PAPI_MAX_STR_LEN];

        int retval = PAPI_event_code_to_name(code, event_name);
        if (retval != PAPI_OK)
            return { error_code::SETUP_ERROR, PAPI_strerror(retval) };
        retval = PAPI_get_event_info(code, &evinfo);
        if (retval != PAPI_OK)
            return { error_code::SETUP_ERROR, PAPI_strerror(retval) };

        size_t pos;
        result<rapl_domain> domain = domain_from_event_name(event_name, pos);
        if (!domain)
        {
            std::cerr << fileline(__FILE__, __LINE__, domain.error().msg()) << "\n";
            continue;
        }
        result<double> mult = multiplier_from_units(evinfo.units);
        if (!mult)
        {
            std::cerr << fileline(__FILE__, __LINE__, mult.error().msg()) << "\n";
            continue;
        }
        uint8_t skt = skt_from_event_name(event_name, pos);
        if (skt > MAX_SOCKETS)
        {
            std::cerr << fileline(__FILE__, __LINE__, "found more sockets than supported\n");
            continue;
        }

        // if domain and socket are to be evaluated in their respective masks
        if ((_dmask & domain.value()) != rapl_domain::NONE && skt_is_set(_skt_mask, skt))
        {
            if (PAPI_add_event(_evset, code) != PAPI_OK)
            {
                std::cout << fileline(__FILE__, __LINE__, "event limit reached\n");
                break;
            }
            events[skt].domain |= domain.value();
            events[skt].mult = mult.value();
            switch (domain.value())
            {
            case rapl_domain::PKG:
                events[skt].pkg = event_idx;
                break;
            case rapl_domain::PP0:
                events[skt].pp0 = event_idx;
                break;
            case rapl_domain::PP1:
                events[skt].pp1 = event_idx;
                break;
            case rapl_domain::DRAM:
                events[skt].dram = event_idx;
                break;
            default:
                break;
            }
            event_idx++;
            std::cout << fileline(__FILE__, __LINE__, std::string("added event: ").append(event_name)) << "\n";
        }
    }
    if (!event_idx)
        return { error_code::SETUP_ERROR, "No events added" };

    for (uint8_t i = 0; i < MAX_SOCKETS; i++)
    {
        _events[i] = event_cpu(
            events[i].domain,
            events[i].pkg,
            events[i].pp0,
            events[i].pp1,
            events[i].dram,
            events[i].mult);
    }
    return error::success();
}

error reader_rapl::read(sample& s) const
{
    int retval = PAPI_read(_evset, s.values());
    if (retval != PAPI_OK)
        return error(error_code::READ_ERROR, PAPI_strerror(retval));
    return error::success();
}

const event_cpu& reader_rapl::event(size_t skt) const
{
    return _events[skt];
}
