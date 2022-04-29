#pragma once

#include "fwd.hpp"

#include <nlohmann/json.hpp>

namespace tep {
struct output_writer {
  nlohmann::json json;

  output_writer() = default;
  output_writer(const output_writer &) = delete;
  output_writer &operator=(const output_writer &) = delete;
};
} // namespace tep
