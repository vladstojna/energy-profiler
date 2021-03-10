// energy_reader_papi.cpp
#include "energy_reader_papi.h"
#include "macros.h"

#include <cassert>
#include <papi.h>

tep::energy_reader_papi::sample_point::sample_point(const timepoint_t& tp,
    size_t num_events) :
    basic_sample(tp),
    values(num_events, 0)
{
}

tep::energy_reader_papi::energy_reader_papi(size_t init_sample_count) :
    energy_reader(),
    _event_set(PAPI_NULL),
    _samples()
{
    int retval = PAPI_library_init(PAPI_VER_CURRENT);
    if (retval != PAPI_VER_CURRENT)
        throw energy_reader_exception("PAPI_library_init failed");
    retval = PAPI_create_eventset(&_event_set);
    if (retval != PAPI_OK)
        throw energy_reader_exception("PAPI_create_eventset failed");
    _samples.reserve(init_sample_count);
}

tep::energy_reader_papi::energy_reader_papi(energy_reader_papi&& other) :
    _event_set(std::exchange(other._event_set, PAPI_NULL)),
    _samples(std::move(other._samples))
{
}

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

int tep::energy_reader_papi::find_component(const char* cmp_name)
{
    assert(cmp_name != nullptr);
    int numcmp = PAPI_num_components();
    for (int cid = 0; cid < numcmp; cid++)
    {
        const PAPI_component_info_t* cmpinfo;
        if ((cmpinfo = PAPI_get_component_info(cid)) == NULL)
            throw tep::energy_reader_exception("PAPI_get_component_info failed");
        std::string_view name(cmpinfo->name,
            sizeof(cmpinfo->name) / sizeof(cmpinfo->name[0]));
        if (name.find(cmp_name) != name.npos)
        {
            int cmpcid = cid;
            dbg(printf("found %s component at cid %d\n", cmp_name, cmpcid));
            if (cmpinfo->disabled)
            {
                constexpr size_t sz = 192;
                char msg[sz];
                snprintf(msg, sz, "%s component disabled: %s", cmp_name,
                    cmpinfo->disabled_reason);
                throw tep::energy_reader_exception(msg);
            }
            return cmpcid;
        }
    }
    constexpr size_t sz = 128;
    char msg[sz];
    snprintf(msg, sz, "no %s component found"
        "configure PAPI with --with-components=\"%s\"", cmp_name, cmp_name);
    throw tep::energy_reader_exception(msg);
}
