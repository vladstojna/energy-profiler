// util.hpp

#pragma once

#include <cstdint>
#include <cinttypes>
#include <signal.h>
#include <sys/ptrace.h>
#include <sys/wait.h>

#if defined(__x86_64__) || defined(__i386__)
#include <sys/user.h>
#endif

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


#if defined(__x86_64__) || defined(__i386__)

    using cpu_regs = user_regs_struct;

#elif defined(__powerpc64__)

    using cpu_regs = pt_regs;

#else

    namespace detail
    {
        struct empty_regs_struct {};
    }

    using cpu_regs = detail::empty_regs_struct;

#endif // __x86_64__ || __i386__


    int get_entrypoint_addr(pid_t pid, uintptr_t& addr);

    uintptr_t get_ip(const cpu_regs& regs);

    void set_ip(cpu_regs& regs, uintptr_t addr);

    void rewind_trap(cpu_regs& regs);

    long set_trap(long word);


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

    const char* sig_str(int signal);

    constexpr int get_ptrace_opts(bool trace_children)
    {
        // kill the tracee when the profiler errors
        int opts = PTRACE_O_EXITKILL;
        if (trace_children)
        {
            // trace threads being spawned using clone()
            opts |= PTRACE_O_TRACECLONE;
            // trace children spawned with fork(): will most likely be useless
            opts |= PTRACE_O_TRACEFORK;
            // trace children spawned with vfork() i.e. clone() with CLONE_VFORK
            opts |= PTRACE_O_TRACEVFORK;
        }
        // trace when a tracee exits
        opts |= PTRACE_O_TRACEEXIT;
        // distinguish normal traps from syscall traps
        opts |= PTRACE_O_TRACESYSGOOD;
        return opts;
    }

}
