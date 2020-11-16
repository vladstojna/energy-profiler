// energy_reader_papi.cpp
#include "energy_reader_papi.h"
#include "macros.h"

#include <cassert>
#include <cinttypes>

#include <string_view>
#include <charconv>
#include <ostream>
#include <sstream>

// helper functions

static int find_rapl_component()
{
    int numcmp = PAPI_num_components();
    int rapl_cid;
    int cid;
    for (cid = 0; cid < numcmp; cid++)
    {
        const PAPI_component_info_t* cmpinfo;
        if ((cmpinfo = PAPI_get_component_info(cid)) == NULL)
            throw tep::energy_reader_exception("PAPI_get_component_info failed");
        std::string_view name(cmpinfo->name,
            sizeof(cmpinfo->name) / sizeof(cmpinfo->name[0]));
        if (name.find("rapl") != name.npos)
        {
            rapl_cid = cid;
            dbg(printf("found rapl component at cid %d\n", rapl_cid));
            if (cmpinfo->disabled)
            {
                constexpr size_t sz = 160;
                char msg[sz];
                int res = snprintf(msg, sz, "RAPL component disabled: %s",
                    cmpinfo->disabled_reason);
                assert(res >= 0 && size_t(res) < sz);
                throw tep::energy_reader_exception(msg);
            }
            return rapl_cid;
        }
    }
    assert(cid == numcmp);
    throw tep::energy_reader_exception("no rapl component found"
        "configure PAPI with --with-component=\"rapl\"");
}

// end helper functions

tep::energy_reader_papi::sample_point::sample_point(uint64_t count, size_t num_events) :
    basic_sample(count),
    values(num_events)
{
}

tep::energy_reader_papi::event_data::event_data(const std::string_view& name,
    const std::string_view& units) :
    type(type::none),
    socket(0),
    multiplier(1)
{
    constexpr const char ANY_PKG_ENERGY[] = "PACKAGE_ENERGY:PACKAGE";
    constexpr const char ANY_DRAM_ENERGY[] = "DRAM_ENERGY:PACKAGE";

    if (units.find("nJ") == units.npos)
        return;
    multiplier = 1e-9;

    size_t pos;
    if ((pos = name.find(ANY_PKG_ENERGY)) != name.npos)
        type = type::pkg_energy;
    else if ((pos = name.find(ANY_DRAM_ENERGY)) != name.npos)
        type = type::dram_energy;
    else
        return;

    auto [ptr, ec] = std::from_chars(&name.at(pos), &name.back(), socket, 10);
    if (ec == std::errc())
        throw energy_reader_exception("unable to find socket number from event name");
}

tep::energy_reader_papi::energy_reader_papi() :
    energy_reader(),
    _event_set(PAPI_NULL),
    _samples(),
    _events()
{
    int retval = PAPI_library_init(PAPI_VER_CURRENT);
    if (retval != PAPI_VER_CURRENT)
        throw energy_reader_exception("PAPI_library_init failed");
    int rapl_cid = find_rapl_component();
    retval = PAPI_create_eventset(&_event_set);
    if (retval != PAPI_OK)
        throw energy_reader_exception("PAPI_create_eventset failed");

    add_events(rapl_cid);
    if (_events.size() == 0)
        throw energy_reader_exception("No events were added");

    _samples.reserve(ISAMPLE_SIZE);
}

tep::energy_reader_papi::energy_reader_papi(energy_reader_papi&& other) = default;

tep::energy_reader_papi::~energy_reader_papi()
{
    int retval = PAPI_cleanup_eventset(_event_set);
    if (retval != PAPI_OK)
        PAPI_perror(fileline("PAPI_cleanup_eventset"));

    retval = PAPI_destroy_eventset(&_event_set);
    if (retval != PAPI_OK)
        PAPI_perror(fileline("PAPI_cleanup_eventset"));

    PAPI_shutdown();
}

void tep::energy_reader_papi::add_events(int cid)
{
    int code = PAPI_NATIVE_MASK;
    int cmp_enum = PAPI_enum_cmp_event(&code, PAPI_ENUM_FIRST, cid);

    while (cmp_enum == PAPI_OK)
    {
        PAPI_event_info_t evinfo;
        char event_name[PAPI_MAX_STR_LEN];

        int retval = PAPI_event_code_to_name(code, event_name);
        if (retval != PAPI_OK)
            throw energy_reader_exception("PAPI_event_code_to_name failed");
        retval = PAPI_get_event_info(code, &evinfo);
        if (retval != PAPI_OK)
            throw energy_reader_exception("PAPI_get_event_info failed");

        event_data evdata(
            std::string_view(event_name, PAPI_MAX_STR_LEN),
            std::string_view(evinfo.units, PAPI_MIN_STR_LEN));

        if (evdata.type != event_data::type::none)
        {
            retval = PAPI_add_event(_event_set, code);
            if (retval != PAPI_OK)
                break; // event limit hit
            _events.push_back(evdata);
        }

        cmp_enum = PAPI_enum_cmp_event(&code, PAPI_ENUM_EVENTS, cid);
    }
}

void tep::energy_reader_papi::start()
{
    assert(_samples.size() == 0);
    _samples.emplace_back(_samples.size(), _events.size());
    PAPI_start(_event_set);
    timepoint_now();
}

void tep::energy_reader_papi::sample()
{
    assert(_samples.size() > 0);
    PAPI_stop(_event_set, _samples.back().values.data());
    _samples.back().duration = timepoint_update();
    _samples.emplace_back(_samples.size(), _events.size());
    PAPI_start(_event_set);
}

void tep::energy_reader_papi::stop()
{
    assert(_samples.size() > 0);
    PAPI_stop(_event_set, _samples.back().values.data());
    _samples.back().duration = timepoint_update();
}

void tep::energy_reader_papi::print(std::ostream & os) const
{
    struct output_values
    {
        double pkg_energy = 0;
        double dram_energy = 0;
    };

    assert(_samples.size() > 0);
    os << "# results\n";

    constexpr size_t sz = 256;
    char buffer[sz];

    const PAPI_hw_info_t* hw_info = PAPI_get_hardware_info();
    std::vector<output_values> outputs(hw_info->sockets);

    for (const auto& sample : _samples)
    {
        assert(sample.values.size() >= outputs.size());
        for (size_t ix = 0; ix < _events.size(); ix++)
        {
            assert(sample.values.size() == _events.size());
            assert(_events[ix].socket < outputs.size());
            switch (_events[ix].type)
            {
            case event_data::type::pkg_energy:
                outputs[_events[ix].socket].pkg_energy =
                    sample.values[ix] * _events[ix].multiplier;
                break;
            case event_data::type::dram_energy:
                outputs[_events[ix].socket].dram_energy =
                    sample.values[ix] * _events[ix].multiplier;
                break;
            default:
                assert(false);
            }
        }
        char* currptr = buffer;
        char* end = buffer + sz;
        currptr += snprintf(currptr, end - currptr, "%" PRIu64 ",%" PRId64,
            sample.number, sample.duration.count());
        assert(currptr < end);
        for (const auto& out : outputs)
        {
            ptrdiff_t diff = (currptr < end ? end - currptr : 0);
            currptr += snprintf(currptr, diff, ",%.6f,%.6f", out.pkg_energy, out.dram_energy);
        }
        os << buffer << '\n';
    }
}
