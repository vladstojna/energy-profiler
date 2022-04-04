#pragma once

#include <array>
#include <cstdint>

namespace tep {
struct syscall_entry {
  static constexpr auto max_args = 6UL;

  uint64_t number;
  std::array<uint64_t, max_args> args;
};
} // namespace tep
