// registers.hpp

#pragma once

#include "syscall_types.hpp"

#include <util/expectedfwd.hpp>

#include <cstddef>
#include <cstdint>
#include <sys/types.h>
#include <sys/user.h>

namespace tep {

class tracer_error;

class cpu_gp_regs {
private:
#if defined(__x86_64__) || defined(__i386__)
  using cpu_regs = user_regs_struct;
#elif defined(__powerpc64__)
  using cpu_regs = pt_regs;
#endif // defined(__x86_64__) || defined(__i386__)

  struct iovec {
    void *base = nullptr;
    size_t len = 0;
  };

  pid_t _pid;
  iovec _iov;
  cpu_regs _regs;

public:
  cpu_gp_regs(pid_t pid);

  tracer_error getregs();
  tracer_error setregs();

  uintptr_t get_ip() const noexcept;
  void set_ip(uintptr_t addr) noexcept;
  void rewind_trap() noexcept;
  syscall_entry get_syscall_entry() const noexcept;

  uintptr_t get_stack_pointer() const noexcept;

  nonstd::expected<uintptr_t, tracer_error> get_return_address() const noexcept;
};

} // namespace tep
