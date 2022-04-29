#include "output_writer.hpp"

namespace tep {
std::ostream &operator<<(std::ostream &os, const output_writer &x) {
  os << x.json;
  return os;
}
} // namespace tep
