// util.hpp

#pragma once

#include <cstdint>
#include <cinttypes>
#include <cstddef>
#include <sys/types.h>

#if __GLIBC__ == 2 && __GLIBC_MINOR__ < 30
#include <unistd.h>
#include <sys/syscall.h>
inline pid_t gettid()
{
    return syscall(SYS_gettid);
}
#endif

namespace tep
{
    int get_entrypoint_addr(pid_t pid, uintptr_t& addr);

    long set_trap(long word);

    bool is_clone_event(int wait_status);
    bool is_vfork_event(int wait_status);
    bool is_fork_event(int wait_status);
    bool is_child_event(int wait_status);
    bool is_exit_event(int wait_status);
    bool is_breakpoint_trap(int wait_status);
    bool is_syscall_trap(int wait_status);

    const char* sig_str(int signal);

    int get_ptrace_opts(bool trace_children);
}
