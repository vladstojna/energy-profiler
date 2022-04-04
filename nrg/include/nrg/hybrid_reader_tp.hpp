#pragma once

#include <nrg/detail/all_reader_ptrs.hpp>
#include <nrg/reader.hpp>

#include <tuple>
#include <type_traits>

namespace nrgprf {
class sample;

// owning hybrid reader
template <typename... Ts> class hybrid_reader_tp : public reader {
  static_assert(sizeof...(Ts) > 0, "At least one Ts must be provided");
  static_assert(
      std::conjunction_v<std::is_same<Ts, detail::remove_cvref_t<Ts>>...>,
      "Ts must be non-const, non-volatile, non-reference types");

private:
  std::tuple<Ts...> _readers;

  struct caller {
    sample &_sample;
    std::error_code &_ec;

    caller(sample &, std::error_code &);

    template <typename First, typename... Rest>
    bool operator()(const First &, const Rest &...);
  };

public:
  using reader::read;

  template <
      typename... Readers,
      std::enable_if_t<detail::all_reader_ptrs_v<Readers...>, bool> = true>
  explicit hybrid_reader_tp(Readers &&...);

  ~hybrid_reader_tp();
  hybrid_reader_tp(const hybrid_reader_tp &);
  hybrid_reader_tp(hybrid_reader_tp &);
  hybrid_reader_tp(hybrid_reader_tp &&);

  hybrid_reader_tp &operator=(const hybrid_reader_tp &);
  hybrid_reader_tp &operator=(hybrid_reader_tp &);
  hybrid_reader_tp &operator=(hybrid_reader_tp &&);

  template <typename T> const T &get() const;
  template <typename T> T &get();

  bool read(sample &, std::error_code &) const override;
  bool read(sample &, uint8_t, std::error_code &) const override;
  size_t num_events() const noexcept override;
};

template <typename... Ts>
hybrid_reader_tp(Ts &&...) -> hybrid_reader_tp<std::remove_reference_t<Ts>...>;
} // namespace nrgprf

#include <nrg/detail/hybrid_reader_tp.inl>
