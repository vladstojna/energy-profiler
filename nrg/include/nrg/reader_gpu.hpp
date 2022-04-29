// reader_gpu.hpp

#pragma once

#include <nrg/reader.hpp>
#include <nrg/readings_type.hpp>
#include <nrg/types.hpp>

#include <iostream>
#include <memory>
#include <vector>

namespace nrgprf {
class sample;

class reader_gpu final : public reader {
private:
  struct impl;
  std::unique_ptr<impl> _impl;

public:
  using reader::read;

  static result<readings_type::type> support(device_mask);
  static result<readings_type::type> support();

  explicit reader_gpu(readings_type::type, device_mask,
                      std::ostream & = std::cout);
  explicit reader_gpu(readings_type::type, std::ostream & = std::cout);
  explicit reader_gpu(device_mask, std::ostream & = std::cout);
  explicit reader_gpu(std::ostream & = std::cout);

  reader_gpu(const reader_gpu &);
  reader_gpu &operator=(const reader_gpu &);

  reader_gpu(reader_gpu &&) noexcept;
  reader_gpu &operator=(reader_gpu &&) noexcept;

  ~reader_gpu();

  bool read(sample &, std::error_code &) const override;
  bool read(sample &, uint8_t, std::error_code &) const override;
  size_t num_events() const noexcept override;

  int8_t event_idx(readings_type::type, uint8_t) const noexcept;

  result<units_power> get_board_power(const sample &, uint8_t) const noexcept;

  result<units_energy> get_board_energy(const sample &, uint8_t) const noexcept;

  std::vector<std::pair<uint32_t, units_power>>
  get_board_power(const sample &) const;

  std::vector<std::pair<uint32_t, units_energy>>
  get_board_energy(const sample &) const;

private:
  const impl *pimpl() const noexcept;
  impl *pimpl() noexcept;
};

} // namespace nrgprf
