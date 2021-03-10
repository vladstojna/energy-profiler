// energy_reader_smp.cpp
#include "energy_reader_smp.h"
#include "macros.h"

#include <cassert>
#include <cinttypes>
#include <string_view>
#include <charconv>
#include <ostream>
#include <sstream>

#include <papi.h>

tep::energy_reader_smp::event_data::event_data(const std::string_view& name,
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

tep::energy_reader_smp::energy_reader_smp(size_t init_sample_count) :
    energy_reader_papi(init_sample_count),
    _events()
{
    int rapl_cid = find_component("rapl");
    add_events(rapl_cid);
    if (_events.size() == 0)
        throw energy_reader_exception("No events were added");
}

tep::energy_reader_smp::energy_reader_smp(energy_reader_smp&& other) :
    energy_reader_papi(std::move(other)),
    _events(std::move(other._events))
{
}

void tep::energy_reader_smp::add_events(int cid)
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
            if (PAPI_add_event(event_set(), code) != PAPI_OK)
            {
                fprintf(stderr, "event limit hit\n");
                break;
            }
            _events.push_back(evdata);
        }

        cmp_enum = PAPI_enum_cmp_event(&code, PAPI_ENUM_EVENTS, cid);
    }
}

void tep::energy_reader_smp::start()
{
    assert(samples().size() == 0);
    samples().emplace_back(now(), _events.size());
    PAPI_start(event_set());
}

void tep::energy_reader_smp::sample()
{
    assert(samples().size() > 0);
    auto& sample = samples().emplace_back(now(), _events.size());
    PAPI_read(event_set(), sample.values.data());
}

void tep::energy_reader_smp::stop()
{
    assert(samples().size() > 0);
    auto& sample = samples().emplace_back(now(), _events.size());
    PAPI_stop(event_set(), sample.values.data());
}

void tep::energy_reader_smp::print(std::ostream& os) const
{
    // always at least two samples
    assert(samples().size() > 1);

    struct output_values
    {
        double pkg_energy = 0;
        double dram_energy = 0;
    };

    os << "# smp results\n";
    os << "# sample_count,duration_ns[,skt0_pkg_energy,skt0_dram_energy...]\n";
    constexpr size_t sz = 256;
    char buffer[sz];
    const PAPI_hw_info_t* hw_info = PAPI_get_hardware_info();
    std::vector<output_values> outputs(hw_info->sockets);

    for (size_t s = 1; s < samples().size(); s++)
    {
        const auto& sample_prev = samples()[s - 1];
        const auto& sample = samples()[s];
        assert(sample_prev.values.size() >= outputs.size());
        assert(sample.values.size() >= outputs.size());

        for (size_t ix = 0; ix < _events.size(); ix++)
        {
            const auto& event = _events[ix];
            assert(sample_prev.values.size() >= outputs.size());
            assert(sample.values.size() == _events.size());
            assert(event.socket < outputs.size());

            switch (event.type)
            {
            case event_data::type::pkg_energy:
                outputs[event.socket].pkg_energy =
                    (sample.values[ix] - sample_prev.values[ix]) *
                    event.multiplier;
                break;
            case event_data::type::dram_energy:
                outputs[event.socket].dram_energy =
                    (sample.values[ix] - sample_prev.values[ix]) *
                    event.multiplier;
                break;
            default:
                assert(false);
            }
        }
        char* currptr = buffer;
        char* end = buffer + sz;
        // we start counting from zero, so consider sample_prev as the sample number
        currptr += snprintf(currptr, end - currptr, "%" PRIu64 ",%" PRId64,
            s - 1, (sample - sample_prev).count());
        assert(currptr < end);
        for (const auto& out : outputs)
        {
            ptrdiff_t diff = (currptr < end ? end - currptr : 0);
            currptr += snprintf(currptr, diff, ",%.8f,%.8f", out.pkg_energy, out.dram_energy);
        }
        os << buffer << '\n';
    }
}
