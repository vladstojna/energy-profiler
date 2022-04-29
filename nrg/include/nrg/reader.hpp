// reader.hpp

#pragma once

#include <cstddef>
#include <cstdint>
#include <system_error>

#include <nrg/types.hpp>

namespace nrgprf {
class sample;

class reader {
protected:
  ~reader() = default;

public:
  virtual bool read(sample &, std::error_code &) const = 0;
  virtual bool read(sample &, uint8_t, std::error_code &) const = 0;

  virtual size_t num_events() const noexcept = 0;

  void read(sample &) const;
  void read(sample &, uint8_t) const;

  result<sample> read() const;
  result<sample> read(uint8_t) const;
};
} // namespace nrgprf
