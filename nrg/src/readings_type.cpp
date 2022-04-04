#include <nrg/readings_type.hpp>

#include <type_traits>

namespace nrgprf::readings_type {
type operator|(type lhs, type rhs) noexcept {
  return static_cast<type>(static_cast<std::underlying_type_t<type>>(lhs) |
                           static_cast<std::underlying_type_t<type>>(rhs));
}

type operator&(type lhs, type rhs) noexcept {
  return static_cast<type>(static_cast<std::underlying_type_t<type>>(lhs) &
                           static_cast<std::underlying_type_t<type>>(rhs));
}

type operator^(type lhs, type rhs) noexcept {
  return static_cast<type>(static_cast<std::underlying_type_t<type>>(lhs) ^
                           static_cast<std::underlying_type_t<type>>(rhs));
}

const type all = power | energy;
} // namespace nrgprf::readings_type
