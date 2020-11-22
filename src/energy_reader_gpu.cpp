// energy_reader_gpu.cpp
#include "energy_reader_gpu.h"
#include "macros.h"

#include <cassert>
#include <cinttypes>
#include <string_view>
#include <charconv>
#include <ostream>
#include <sstream>

#include <papi.h>

uint32_t get_device_count()
{
    constexpr const char event_name[] = "rocm_smi:::NUMDevices";
    int event_set = PAPI_NULL;
    int event_code;
    long long dev_cnt;
    if (PAPI_event_name_to_code(event_name, &event_code) != PAPI_OK)
        throw tep::energy_reader_exception("PAPI_event_name_to_code failed");
    if (PAPI_create_eventset(&event_set) != PAPI_OK)
        throw tep::energy_reader_exception("PAPI_create_eventset failed");
    if (PAPI_add_event(event_set, event_code) != PAPI_OK)
        throw tep::energy_reader_exception("PAPI_add_event failed");
    if (PAPI_start(event_set) != PAPI_OK)
        throw tep::energy_reader_exception("PAPI_start failed");
    if (PAPI_stop(event_set, &dev_cnt) != PAPI_OK)
        throw tep::energy_reader_exception("PAPI_stop failed");
    if (PAPI_cleanup_eventset(event_set) != PAPI_OK)
        throw tep::energy_reader_exception("PAPI_cleanup_eventset failed");
    if (PAPI_destroy_eventset(&event_set) != PAPI_OK)
        throw tep::energy_reader_exception("PAPI_destroy_eventset failed");
    if (dev_cnt < 1)
        throw tep::energy_reader_exception("There are no GPUs to manage");
    return static_cast<uint32_t>(dev_cnt);
}

tep::energy_reader_gpu::event_data::event_data(const std::string_view& name) :
    type(type::none),
    device(0),
    multiplier(1e-6) // microWatts
{
    constexpr const char DEVICE[] = "device=";
    constexpr const size_t DEV_SZ = 7;
    constexpr const char AVG_POWER[] = "power_average:";

    size_t pos;
    if ((pos = name.find(DEVICE)) != name.npos && name.find(AVG_POWER) != name.npos)
    {
        type = type::avg_power;
        auto [ptr, ec] = std::from_chars(&name.at(pos + DEV_SZ), &name.back(), device, 10);
        if (ec == std::errc())
            throw energy_reader_exception("unable to find device number from event name");
    }
}

tep::energy_reader_gpu::energy_reader_gpu(size_t init_sample_count) :
    energy_reader_papi(init_sample_count)
{
    add_events(find_component("rocm_smi"));
}

tep::energy_reader_gpu::energy_reader_gpu(energy_reader_gpu&& other) :
    energy_reader_papi(std::move(other)),
    _events(std::move(other._events))
{
}

void tep::energy_reader_gpu::add_events(int cid)
{
    uint32_t dev_cnt = get_device_count();
    int code = PAPI_NATIVE_MASK;
    int cmp_enum = PAPI_enum_cmp_event(&code, PAPI_ENUM_FIRST, cid);

    _events.reserve(dev_cnt);
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

        dbg(printf(fileline("event name: %.128s, units: %.64s\n"),
            event_name, evinfo.units));
        event_data evdata(std::string_view(event_name, PAPI_MAX_STR_LEN));

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
    if (_events.size() != dev_cnt)
    {
        constexpr size_t sz = 128;
        char buffer[sz];
        snprintf(buffer, sz, "incorrect number of events found: "
            "%zu events, %" PRIu32 " devices", _events.size(), dev_cnt);
        throw tep::energy_reader_exception(buffer);
    }
}

void tep::energy_reader_gpu::start()
{
    assert(samples().size() == 0);
    samples().emplace_back(now(), _events.size());
    PAPI_start(event_set());
}

void tep::energy_reader_gpu::sample()
{
    assert(samples().size() > 0);
    auto& sample = samples().emplace_back(now(), _events.size());
    PAPI_read(event_set(), sample.values.data());
}

void tep::energy_reader_gpu::stop()
{
    assert(samples().size() > 0);
    auto& sample = samples().emplace_back(now(), _events.size());
    PAPI_stop(event_set(), sample.values.data());
}

void tep::energy_reader_gpu::print(std::ostream& os) const
{
    // always at least two samples
    assert(samples().size() > 1);
    os << "# gpu results\n";
    os << "# sample_count,duration_ns[,device_num,energy...]\n";
    // TODO finish this function
}
