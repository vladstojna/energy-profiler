// sample.hpp

#pragma once

#include <nrg/arch/sample_data.hpp>
#include <nrg/constants.hpp>

#include <array>
#include <chrono>
#include <cstdint>

namespace nrgprf {
class reader;
class reader_rapl;
class reader_gpu;

class sample {
public:
  using value_type = uint64_t;

  detail::sample_data data;

  sample();

  bool operator==(const sample &rhs) const;
  bool operator!=(const sample &rhs) const;

  explicit operator bool() const;
};
} // namespace nrgprf
