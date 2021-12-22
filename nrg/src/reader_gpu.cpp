// reader_gpu.cpp

#include "visibility.hpp"
#include "reader_gpu.hpp"
#include "create_reader.hpp"

#include <nrg/reader_gpu.hpp>
#include <nrg/sample.hpp>

#include <nonstd/expected.hpp>

using namespace nrgprf;

struct NRG_LOCAL reader_gpu::impl : reader_gpu_impl
{
    using reader_gpu_impl::reader_gpu_impl;
};

result<reader_gpu> reader_gpu::create(
    readings_type::type rt, device_mask dm, std::ostream& os)
{
    return create_reader_impl<reader_gpu>(os, rt, dm);
}

result<reader_gpu> reader_gpu::create(readings_type::type rt, std::ostream& os)
{
    return create_reader_impl<reader_gpu>(os, rt);
}

result<reader_gpu> reader_gpu::create(device_mask dm, std::ostream& os)
{
    return create_reader_impl<reader_gpu>(os, dm);
}

result<reader_gpu> reader_gpu::create(std::ostream& os)
{
    return create_reader_impl<reader_gpu>(os);
}

result<readings_type::type> reader_gpu::support(device_mask devmask)
{
    return impl::support(devmask);
}

result<readings_type::type> reader_gpu::support()
{
    return support(device_mask(0xff));
}

reader_gpu::reader_gpu(readings_type::type rt, device_mask dev_mask, error& ec, std::ostream& os) :
    _impl(std::make_unique<reader_gpu::impl>(rt, dev_mask, ec, os))
{}

reader_gpu::reader_gpu(readings_type::type rt, error& ec, std::ostream& os) :
    reader_gpu(rt, device_mask(0xff), ec, os)
{}

reader_gpu::reader_gpu(device_mask dev_mask, error& ec, std::ostream& os) :
    reader_gpu(readings_type::all, dev_mask, ec, os)
{}

reader_gpu::reader_gpu(error& ec, std::ostream& os) :
    reader_gpu(readings_type::all, device_mask(0xff), ec, os)
{}

reader_gpu::reader_gpu(const reader_gpu& other) :
    _impl(std::make_unique<reader_gpu::impl>(*other.pimpl()))
{}

reader_gpu& reader_gpu::operator=(const reader_gpu& other)
{
    _impl = std::make_unique<reader_gpu::impl>(*other.pimpl());
    return *this;
}

reader_gpu::reader_gpu(reader_gpu&& other) = default;
reader_gpu& reader_gpu::operator=(reader_gpu && other) = default;
reader_gpu::~reader_gpu() = default;

error reader_gpu::read(sample & s) const
{
    return pimpl()->read(s);
}

error reader_gpu::read(sample & s, uint8_t ev_idx) const
{
    return pimpl()->read(s, ev_idx);
}

result<sample> reader_gpu::read() const
{
    sample s;
    if (error err = read(s))
        return result<sample>{ nonstd::unexpect, std::move(err) };
    return s;
}

result<sample> reader_gpu::read(uint8_t idx) const
{
    sample s;
    if (error err = read(s, idx))
        return result<sample>{ nonstd::unexpect, std::move(err) };
    return s;
}

int8_t reader_gpu::event_idx(readings_type::type rt, uint8_t device) const
{
    return pimpl()->event_idx(rt, device);
}

size_t reader_gpu::num_events() const
{
    return pimpl()->num_events();
}

result<units_power> reader_gpu::get_board_power(const sample & s, uint8_t dev) const
{
    return pimpl()->get_board_power(s, dev);
}

result<units_energy> reader_gpu::get_board_energy(const sample & s, uint8_t dev) const
{
    return pimpl()->get_board_energy(s, dev);
}

const reader_gpu::impl* reader_gpu::pimpl() const
{
    assert(_impl);
    return _impl.get();
}

reader_gpu::impl* reader_gpu::pimpl()
{
    assert(_impl);
    return _impl.get();
}

#define DEFINE_GET_METHOD(rettype, method) \
std::vector<std::pair<uint32_t, rettype>> \
reader_gpu::method(const sample& s) const \
{ \
    std::vector<std::pair<uint32_t, rettype>> retval; \
    for (uint32_t d = 0; d < max_devices; d++) \
    { \
        if (auto val = method(s, d)) \
            retval.push_back({ d, *std::move(val) }); \
    } \
    return retval; \
}

DEFINE_GET_METHOD(units_power, get_board_power)
DEFINE_GET_METHOD(units_energy, get_board_energy)
