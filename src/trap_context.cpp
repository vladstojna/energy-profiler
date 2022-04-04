#include "trap_context.hpp"

namespace tep {
bool trap_context::is_function_call() const noexcept {
  return self_->is_function_call();
}

uintptr_t trap_context::addr() const noexcept { return self_->addr(); }

std::string to_string(const trap_context &x) { return x.self_->as_string(); }

std::ostream &operator<<(std::ostream &os, const trap_context &x) {
  x.self_->print(os);
  return os;
}

output_writer &operator<<(output_writer &os, const trap_context &x) {
  x.self_->print(os);
  return os;
}
} // namespace tep
