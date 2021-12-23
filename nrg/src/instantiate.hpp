#pragma once

#define INSTANTIATE_EVENT_IDX(name, location) \
    template \
    int32_t name::event_idx<nrgprf::loc::location>(uint8_t skt) const

#define INSTANTIATE_VALUE(name, location) \
    template \
    nrgprf::result<nrgprf::sensor_value> \
    name::value<nrgprf::loc::location>(const nrgprf::sample& s, uint8_t skt) const

#define INSTANTIATE_VALUES(name, location) \
    template \
    std::vector<std::pair<uint32_t, nrgprf::sensor_value>> \
    name::values<nrgprf::loc::location>(const nrgprf::sample& s) const

#define INSTANTIATE_ALL(name, macro) \
    macro(name, pkg); \
    macro(name, cores); \
    macro(name, uncore); \
    macro(name, mem); \
    macro(name, sys); \
    macro(name, gpu)
