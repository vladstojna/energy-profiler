// reader_rapl.hpp

#pragma once

#include <nrg/constants.hpp>
#include <nrg/location.hpp>
#include <nrg/reader.hpp>
#include <nrg/types.hpp>

#include <iostream>
#include <memory>
#include <vector>

namespace nrgprf {
class sample;

class reader_rapl final : public reader {
private:
  class impl;
  std::unique_ptr<impl> _impl;

public:
  using reader::read;

  explicit reader_rapl(location_mask, socket_mask, std::ostream & = std::cout);
  explicit reader_rapl(location_mask, std::ostream & = std::cout);
  explicit reader_rapl(socket_mask, std::ostream & = std::cout);
  explicit reader_rapl(std::ostream & = std::cout);

  reader_rapl(const reader_rapl &);
  reader_rapl &operator=(const reader_rapl &);

  reader_rapl(reader_rapl &&) noexcept;
  reader_rapl &operator=(reader_rapl &&) noexcept;

  ~reader_rapl();

  bool read(sample &, std::error_code &) const override;
  bool read(sample &, uint8_t, std::error_code &) const override;

  size_t num_events() const noexcept override;

  template <typename Tag> int32_t event_idx(uint8_t) const noexcept;

  template <typename Location>
  result<sensor_value> value(const sample &, uint8_t) const noexcept;

  template <typename Location>
  std::vector<std::pair<uint32_t, sensor_value>> values(const sample &) const;

private:
  const impl *pimpl() const noexcept;
  impl *pimpl() noexcept;
};
} // namespace nrgprf
