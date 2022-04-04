// reader_gpu.cpp

#include "reader_gpu.hpp"
#include "visibility.hpp"

#include <nrg/reader_gpu.hpp>
#include <nrg/sample.hpp>

#include <nonstd/expected.hpp>

using namespace nrgprf;

struct NRG_LOCAL reader_gpu::impl : reader_gpu_impl {
  using reader_gpu_impl::reader_gpu_impl;
};

result<readings_type::type> reader_gpu::support(device_mask devmask) {
  return impl::support(devmask);
}

result<readings_type::type> reader_gpu::support() {
  return support(device_mask(0xff));
}

reader_gpu::reader_gpu(readings_type::type rt, device_mask dev_mask,
                       std::ostream &os)
    : _impl(std::make_unique<reader_gpu::impl>(rt, dev_mask, os)) {}

reader_gpu::reader_gpu(readings_type::type rt, std::ostream &os)
    : reader_gpu(rt, device_mask(0xff), os) {}

reader_gpu::reader_gpu(device_mask dev_mask, std::ostream &os)
    : reader_gpu(readings_type::all, dev_mask, os) {}

reader_gpu::reader_gpu(std::ostream &os)
    : reader_gpu(readings_type::all, device_mask(0xff), os) {}

reader_gpu::reader_gpu(const reader_gpu &other)
    : _impl(std::make_unique<reader_gpu::impl>(*other.pimpl())) {}

reader_gpu &reader_gpu::operator=(const reader_gpu &other) {
  _impl = std::make_unique<reader_gpu::impl>(*other.pimpl());
  return *this;
}

reader_gpu::reader_gpu(reader_gpu &&) noexcept = default;
reader_gpu &reader_gpu::operator=(reader_gpu &&) noexcept = default;
reader_gpu::~reader_gpu() = default;

bool reader_gpu::read(sample &s, std::error_code &ec) const {
  return pimpl()->read(s, ec);
}

bool reader_gpu::read(sample &s, uint8_t ev_idx, std::error_code &ec) const {
  return pimpl()->read(s, ev_idx, ec);
}

int8_t reader_gpu::event_idx(readings_type::type rt,
                             uint8_t device) const noexcept {
  return pimpl()->event_idx(rt, device);
}

size_t reader_gpu::num_events() const noexcept { return pimpl()->num_events(); }

result<units_power> reader_gpu::get_board_power(const sample &s,
                                                uint8_t dev) const noexcept {
  return pimpl()->get_board_power(s, dev);
}

result<units_energy> reader_gpu::get_board_energy(const sample &s,
                                                  uint8_t dev) const noexcept {
  return pimpl()->get_board_energy(s, dev);
}

const reader_gpu::impl *reader_gpu::pimpl() const noexcept {
  assert(_impl);
  return _impl.get();
}

reader_gpu::impl *reader_gpu::pimpl() noexcept {
  assert(_impl);
  return _impl.get();
}

#define DEFINE_GET_METHOD(rettype, method)                                     \
  std::vector<std::pair<uint32_t, rettype>> reader_gpu::method(                \
      const sample &s) const {                                                 \
    std::vector<std::pair<uint32_t, rettype>> retval;                          \
    for (uint32_t d = 0; d < max_devices; d++) {                               \
      if (auto val = method(s, d))                                             \
        retval.push_back({d, *std::move(val)});                                \
    }                                                                          \
    return retval;                                                             \
  }

DEFINE_GET_METHOD(units_power, get_board_power)
DEFINE_GET_METHOD(units_energy, get_board_energy)
