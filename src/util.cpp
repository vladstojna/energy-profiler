// util.cpp

#include "util.hpp"

#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <cstring>
#include <mutex>

#include <signal.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <unistd.h>

int tep::get_entrypoint_addr(pid_t pid, uintptr_t& addr)
{
    char filename[24];
    if (snprintf(filename, 24, "/proc/%d/maps", pid) >= 24)
        return -1;

    FILE* maps = fopen(filename, "r");
    if (maps == nullptr)
        return -1;

    int rv = fscanf(maps, "%" SCNxPTR, &addr);
    if (rv == 0 || rv == EOF)
    {
        fclose(maps);
        return -1;
    }

    if (fclose(maps) != 0)
        return -1;

    return 0;
}

#if defined(__x86_64__) || defined(__i386__)

long tep::set_trap(long word)
{
    // int3
    return (word & ~0xff) | 0xcc;
}

#elif defined(__powerpc64__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__

long tep::set_trap(long word)
{
    // tw 31, 0, 0
    return (word & 0xffffffff) | (0x7fe00008 << 32);
}

#elif defined(__powerpc64__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__

long tep::set_trap(long word)
{
    constexpr static const long mask = 0xffffffff;
    // tw 31, 0, 0
    return (word & ~mask) | 0x7fe00008;
}

#else

long tep::set_trap(long word)
{
    return word;
}

#endif // __x86_64__ || __i386__


#if __GLIBC__ == 2 && __GLIBC_MINOR__ < 32

const char* tep::sig_str(int signal)
{
    return strsignal(signal);
}

#else

const char* tep::sig_str(int signal)
{
    return sigabbrev_np(signal);
}

#endif

bool tep::is_clone_event(int wait_status)
{
    return wait_status >> 8 == (SIGTRAP | (PTRACE_EVENT_CLONE << 8));
}

bool tep::is_vfork_event(int wait_status)
{
    return wait_status >> 8 == (SIGTRAP | (PTRACE_EVENT_VFORK << 8));
}

bool tep::is_fork_event(int wait_status)
{
    return wait_status >> 8 == (SIGTRAP | (PTRACE_EVENT_FORK << 8));
}

bool tep::is_child_event(int wait_status)
{
    return is_clone_event(wait_status) ||
        is_vfork_event(wait_status) ||
        is_fork_event(wait_status);
}

bool tep::is_exit_event(int wait_status)
{
    return wait_status >> 8 == (SIGTRAP | (PTRACE_EVENT_EXIT << 8));
}

bool tep::is_breakpoint_trap(int wait_status)
{
    return WIFSTOPPED(wait_status) &&
        !(WSTOPSIG(wait_status) & 0x80) &&
        (WSTOPSIG(wait_status) == SIGTRAP);
}

bool tep::is_syscall_trap(int wait_status)
{
    return WIFSTOPPED(wait_status) &&
        WSTOPSIG(wait_status) == (SIGTRAP | 0x80);
}

int tep::get_ptrace_exitkill()
{
    // TODO: find a way to always set option PTRACE_O_EXITKILL
    // should be present since Linux 3.8 but not always defined in ptrace headers
#if !defined(PTRACE_O_EXITKILL)
#define PTRACE_O_EXITKILL (1 << 20)
#endif
    return PTRACE_O_EXITKILL;
}

int tep::get_ptrace_opts(bool trace_children)
{
    int opts = 0;
    // kill the tracee when the profiler errors
    opts = get_ptrace_exitkill();
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
