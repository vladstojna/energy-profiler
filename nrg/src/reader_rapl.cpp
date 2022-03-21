// reader_rapl.cpp

#include "reader_cpu.hpp"
#include "visibility.hpp"

#include <nrg/reader_rapl.hpp>
#include <nrg/sample.hpp>

#include <nonstd/expected.hpp>

#include <cassert>

using namespace nrgprf;

struct NRG_LOCAL reader_rapl::impl : reader_impl
{
    using reader_impl::reader_impl;
};

reader_rapl::reader_rapl(location_mask dmask, socket_mask skt_mask, std::ostream& os) :
    _impl(std::make_unique<reader_rapl::impl>(dmask, skt_mask, os))
{}

reader_rapl::reader_rapl(location_mask dmask, std::ostream& os) :
    reader_rapl(dmask, socket_mask(~0x0), os)
{}

reader_rapl::reader_rapl(socket_mask skt_mask, std::ostream& os) :
    reader_rapl(location_mask(~0x0), skt_mask, os)
{}

reader_rapl::reader_rapl(std::ostream& os) :
    reader_rapl(location_mask(~0x0), socket_mask(~0x0), os)
{}

reader_rapl::reader_rapl(const reader_rapl& other) :
    _impl(std::make_unique<reader_rapl::impl>(*other.pimpl()))
{}

reader_rapl& reader_rapl::operator=(const reader_rapl& other)
{
    _impl = std::make_unique<reader_rapl::impl>(*other.pimpl());
    return *this;
}

reader_rapl::reader_rapl(reader_rapl&&) noexcept = default;
reader_rapl& reader_rapl::operator=(reader_rapl&&) noexcept = default;
reader_rapl::~reader_rapl() = default;

bool reader_rapl::read(sample & s, std::error_code & ec) const
{
    return pimpl()->read(s, ec);
}

bool reader_rapl::read(sample & s, uint8_t idx, std::error_code & ec) const
{
    return pimpl()->read(s, idx, ec);
}

size_t reader_rapl::num_events() const noexcept
{
    return pimpl()->num_events();
}

template<typename Location>
int32_t reader_rapl::event_idx(uint8_t skt) const noexcept
{
    return pimpl()->event_idx<Location>(skt);
}

template<typename Location>
result<sensor_value> reader_rapl::value(const sample & s, uint8_t skt) const noexcept
{
    return pimpl()->value<Location>(s, skt);
}

template<typename Location>
std::vector<std::pair<uint32_t, sensor_value>> reader_rapl::values(const sample & s) const
{
    std::vector<std::pair<uint32_t, sensor_value>> retval;
    for (uint32_t skt = 0; skt < max_sockets; skt++)
    {
        if (auto val = value<Location>(s, skt))
            retval.push_back({ skt, *std::move(val) });
    };
    return retval;
}

const reader_rapl::impl* reader_rapl::pimpl() const noexcept
{
    assert(_impl);
    return _impl.get();
}

reader_rapl::impl* reader_rapl::pimpl() noexcept
{
    assert(_impl);
    return _impl.get();
}

// explicit instantiation

#include "instantiate.hpp"
INSTANTIATE_ALL(reader_rapl, INSTANTIATE_EVENT_IDX);
INSTANTIATE_ALL(reader_rapl, INSTANTIATE_VALUE);
INSTANTIATE_ALL(reader_rapl, INSTANTIATE_VALUES);
