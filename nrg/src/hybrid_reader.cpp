// hybrid_reader.cpp

#include <nrg/error.hpp>
#include <nrg/hybrid_reader.hpp>
#include <nrg/sample.hpp>

#include <nonstd/expected.hpp>

#include <cassert>

using namespace nrgprf;

void hybrid_reader::push_back(const reader &r) { _readers.push_back(&r); }

bool hybrid_reader::read(sample &s, std::error_code &ec) const {
  for (auto r : _readers) {
    assert(r != nullptr);
    if (!r->read(s, ec))
      return false;
  }
  return true;
}

bool hybrid_reader::read(sample &, uint8_t, std::error_code &ec) const {
  ec = errc::operation_not_supported;
  return false;
}

size_t hybrid_reader::num_events() const noexcept {
  size_t total = 0;
  for (auto r : _readers) {
    assert(r != nullptr);
    total += r->num_events();
  }
  return total;
}
