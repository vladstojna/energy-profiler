#pragma once

#include "../visibility.hpp"

#include <nrg/types.hpp>

#include <array>
#include <iosfwd>
#include <vector>

namespace nrgprf {
class sample;

struct NRG_LOCAL file_descriptor {
  int value;

  explicit file_descriptor(const char *file);
  ~file_descriptor() noexcept;

  file_descriptor(const file_descriptor &fd);
  file_descriptor(file_descriptor &&fd) noexcept;
  file_descriptor &operator=(file_descriptor &&other) noexcept;
};

struct NRG_LOCAL event_data {
  file_descriptor fd;
  mutable uint64_t max;
  mutable uint64_t prev;
  mutable uint64_t curr_max;
  event_data(file_descriptor &&fd, uint64_t max) noexcept;
};

struct NRG_LOCAL reader_impl {
  std::array<std::array<int32_t, max_domains>, max_sockets> _event_map;
  std::vector<event_data> _active_events;

  reader_impl(location_mask, socket_mask, std::ostream &);

  bool read(sample &, std::error_code &) const;
  bool read(sample &, uint8_t, std::error_code &) const;
  size_t num_events() const noexcept;

  template <typename Location> int32_t event_idx(uint8_t) const noexcept;

  template <typename Location>
  result<sensor_value> value(const sample &, uint8_t) const noexcept;

private:
  std::error_code add_event(const char *base, location_mask dmask, uint8_t skt,
                            std::ostream &os);
};
} // namespace nrgprf
