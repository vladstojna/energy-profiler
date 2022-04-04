#pragma once

#include <nrg/arch.hpp>
#include <nrg/units.hpp>

namespace nrgprf {
namespace detail {
#if defined NRG_X86_64
using reader_return = microjoules<uintmax_t>;
#elif defined NRG_PPC64
struct reader_return_st {
  using time_point = std::chrono::time_point<std::chrono::steady_clock>;
  time_point timestamp;
  microwatts<uintmax_t> power;
};
using reader_return = reader_return_st;
#endif
} // namespace detail
} // namespace nrgprf
