#pragma once

#include "fwd.hpp"

#include <iosfwd>

namespace tep::dbg {
struct debug_dump {
  const object_info &obj_info;

  explicit debug_dump(const object_info &) noexcept;
  debug_dump(const debug_dump &) = delete;
  debug_dump &operator=(const debug_dump &) = delete;
};

std::ostream &operator<<(std::ostream &, const debug_dump &);
} // namespace tep::dbg
