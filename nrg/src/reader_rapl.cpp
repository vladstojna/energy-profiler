// reader_rapl.cpp

#include "reader_cpu.hpp"
#include "visibility.hpp"
#include "create_reader.hpp"

#include <nrg/reader_rapl.hpp>
#include <nrg/sample.hpp>

#include <nonstd/expected.hpp>

#include <cassert>

using namespace nrgprf;

struct NRG_LOCAL reader_rapl::impl : reader_impl
{
    using reader_impl::reader_impl;
};

result<reader_rapl> reader_rapl::create(
    location_mask lm, socket_mask sm, std::ostream& os)
{
    return create_reader_impl<reader_rapl>(os, lm, sm);
}

result<reader_rapl> reader_rapl::create(location_mask lm, std::ostream& os)
{
    return create_reader_impl<reader_rapl>(os, lm);
}

result<reader_rapl> reader_rapl::create(socket_mask sm, std::ostream& os)
{
    return create_reader_impl<reader_rapl>(os, sm);
}

result<reader_rapl> reader_rapl::create(std::ostream& os)
{
    return create_reader_impl<reader_rapl>(os);
}

reader_rapl::reader_rapl(location_mask dmask, socket_mask skt_mask, error& ec, std::ostream& os) :
    _impl(std::make_unique<reader_rapl::impl>(dmask, skt_mask, ec, os))
{}

reader_rapl::reader_rapl(location_mask dmask, error& ec, std::ostream& os) :
    reader_rapl(dmask, socket_mask(~0x0), ec, os)
{}

reader_rapl::reader_rapl(socket_mask skt_mask, error& ec, std::ostream& os) :
    reader_rapl(location_mask(~0x0), skt_mask, ec, os)
{}

reader_rapl::reader_rapl(error& ec, std::ostream& os) :
    reader_rapl(location_mask(~0x0), socket_mask(~0x0), ec, os)
{}

reader_rapl::reader_rapl(const reader_rapl& other) :
    _impl(std::make_unique<reader_rapl::impl>(*other.pimpl()))
{}

reader_rapl& reader_rapl::operator=(const reader_rapl& other)
{
    _impl = std::make_unique<reader_rapl::impl>(*other.pimpl());
    return *this;
}

reader_rapl::reader_rapl(reader_rapl&& other) = default;
reader_rapl& reader_rapl::operator=(reader_rapl && other) = default;
reader_rapl::~reader_rapl() = default;

error reader_rapl::read(sample & s) const
{
    return pimpl()->read(s);
}

error reader_rapl::read(sample & s, uint8_t idx) const
{
    return pimpl()->read(s, idx);
}

result<sample> reader_rapl::read() const
{
    sample s;
    if (error err = read(s))
        return result<sample>{ nonstd::unexpect, std::move(err) };
    return s;
}

result<sample> reader_rapl::read(uint8_t idx) const
{
    sample s;
    if (error err = read(s, idx))
        return result<sample>{ nonstd::unexpect, std::move(err) };
    return s;
}

size_t reader_rapl::num_events() const
{
    return pimpl()->num_events();
}

template<typename Location>
int32_t reader_rapl::event_idx(uint8_t skt) const
{
    return pimpl()->event_idx<Location>(skt);
}

template<typename Location>
result<sensor_value> reader_rapl::value(const sample & s, uint8_t skt) const
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

const reader_rapl::impl* reader_rapl::pimpl() const
{
    assert(_impl);
    return _impl.get();
}

reader_rapl::impl* reader_rapl::pimpl()
{
    assert(_impl);
    return _impl.get();
}

// explicit instantiation

#include "instantiate.hpp"
INSTANTIATE_ALL(reader_rapl, INSTANTIATE_EVENT_IDX);
INSTANTIATE_ALL(reader_rapl, INSTANTIATE_VALUE);
INSTANTIATE_ALL(reader_rapl, INSTANTIATE_VALUES);
