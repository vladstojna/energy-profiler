#pragma once

#include "../../visibility.hpp"

#include <string>
#include <string_view>

#include <nrg/types.hpp>

namespace nrgprf {
NRG_LOCAL constexpr int bitnum(int v) {
  int num = 0;
  while (!(v & 0x1)) {
    v >>= 1;
    num++;
  }
  return num;
}

NRG_LOCAL result<uint8_t> count_sockets();
} // namespace nrgprf
