// util.hpp

#pragma once

#include <cstdint>
#include <cinttypes>
#include <signal.h>
#include <sys/ptrace.h>
#include <sys/wait.h>


#if __GLIBC__ == 2 && __GLIBC_MINOR__ < 30
#include <unistd.h>
#include <sys/syscall.h>
inline pid_t gettid()
{
    return syscall(SYS_gettid);
}
#endif


#define log(lvl, fmt, ...) \
    log__(__FILE__, __LINE__, lvl, fmt, __VA_ARGS__)


struct user_regs_struct;

namespace tep
{

    enum class log_lvl
    {
        debug,
        info,
        success,
        warning,
        error
    };


    void log__(const char* file, int line, log_lvl lvl, const char* fmt, ...);

    bool timestamp(char* buff, size_t sz);

    uintptr_t get_entrypoint_addr(pid_t pid);

    uintptr_t get_ip(const user_regs_struct& regs);

    void set_ip(user_regs_struct& regs, uintptr_t addr);


    constexpr bool is_clone_event(int wait_status)
    {
        return wait_status >> 8 == (SIGTRAP | (PTRACE_EVENT_CLONE << 8));
    }

    constexpr bool is_vfork_event(int wait_status)
    {
        return wait_status >> 8 == (SIGTRAP | (PTRACE_EVENT_VFORK << 8));
    }

    constexpr bool is_fork_event(int wait_status)
    {
        return wait_status >> 8 == (SIGTRAP | (PTRACE_EVENT_FORK << 8));
    }

    constexpr bool is_child_event(int wait_status)
    {
        return is_clone_event(wait_status) ||
            is_vfork_event(wait_status) ||
            is_fork_event(wait_status);
    }

    constexpr bool is_exit_event(int wait_status)
    {
        return wait_status >> 8 == (SIGTRAP | (PTRACE_EVENT_EXIT << 8));
    }

    constexpr bool is_breakpoint_trap(int wait_status)
    {
        return WIFSTOPPED(wait_status) &&
            !(WSTOPSIG(wait_status) & 0x80) &&
            (WSTOPSIG(wait_status) == SIGTRAP);
    }

    constexpr unsigned long lsb_mask()
    {
        return ~0xff;
    }

    constexpr uint8_t trap_code()
    {
        return 0xcc;
    }

    const char* sig_str(int signal);

}
