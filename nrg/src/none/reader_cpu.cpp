#include "reader_cpu.hpp"
#include "../fileline.hpp"

#include <nonstd/expected.hpp>
#include <nrg/location.hpp>

namespace nrgprf {
reader_impl::reader_impl(location_mask, socket_mask, std::ostream &os) {
  os << fileline("No-op CPU reader\n");
}

bool reader_impl::read(sample &, std::error_code &) const noexcept {
  return true;
}

bool reader_impl::read(sample &, uint8_t, std::error_code &) const noexcept {
  return true;
}

size_t reader_impl::num_events() const noexcept { return 0; }

template <typename Location>
int32_t reader_impl::event_idx(uint8_t) const noexcept {
  return -1;
}

template <typename Location>
result<sensor_value> reader_impl::value(const sample &,
                                        uint8_t) const noexcept {
  return result<sensor_value>(nonstd::unexpect, errc::no_such_event);
}
} // namespace nrgprf

#include "../instantiate.hpp"
INSTANTIATE_ALL(nrgprf::reader_impl, INSTANTIATE_EVENT_IDX);
INSTANTIATE_ALL(nrgprf::reader_impl, INSTANTIATE_VALUE);
