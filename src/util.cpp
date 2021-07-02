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


#define RED "\x1B[31m"
#define GRN "\x1B[32m"
#define YEL "\x1B[33m"
#define BLU "\x1B[34m"
#define CYN "\x1B[36m"
#define RST "\x1B[0m"


constexpr static const char* levels[] =
{
    CYN "debug" RST,
    BLU "info" RST,
    GRN "success" RST,
    YEL "warn" RST,
    RED "error" RST
};


void tep::log__(const char* file, int line, tep::log_lvl lvl, const char* fmt, ...)
{
    static std::mutex log_mutex;
    std::scoped_lock lock(log_mutex);
    va_list args;
    char ts[128];
    FILE* stream = lvl >= tep::log_lvl::warning ? stderr : stdout;
    va_start(args, fmt);
    timestamp(ts, sizeof(ts));
    fprintf(stream, "%s: %s:%-3d %s: ", ts, file, line, levels[static_cast<unsigned>(lvl)]);
    vfprintf(stream, fmt, args);
    fputc('\n', stream);
    va_end(args);
}

bool tep::timestamp(char* buff, size_t sz)
{
    using namespace std::chrono;
    system_clock::time_point stp(system_clock::now());
    microseconds us = duration_cast<microseconds>(stp.time_since_epoch());
    seconds sec = duration_cast<seconds>(us);

    std::tm tm;
    std::time_t time = sec.count();
    localtime_r(&time, &tm);

    size_t tm_sz = std::strftime(buff, sz, "%T", &tm);
    if (!tm_sz)
        return false;
    int written = snprintf(buff + tm_sz, sz - tm_sz, ".%06" PRId64,
        us.count() % microseconds::period::den);
    if (written < 0 || static_cast<unsigned>(written) >= sz - tm_sz)
        return false;
    return true;
}

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


#ifdef __x86_64__

uintptr_t tep::get_ip(const cpu_regs& regs)
{
    return regs.rip;
}

void tep::set_ip(cpu_regs& regs, uintptr_t addr)
{
    regs.rip = addr;
}

#elif defined(__i386__)

uintptr_t tep::get_ip(const cpu_regs& regs)
{
    return regs.eip;
}

void tep::set_ip(cpu_regs& regs, uintptr_t addr)
{
    regs.eip = addr;
}

#elif defined(__powerpc64__)

uintptr_t tep::get_ip(const cpu_regs& regs)
{
    return regs.nip;
}

void tep::set_ip(cpu_regs& regs, uintptr_t addr)
{
    regs.nip = addr;
}

#else

uintptr_t tep::get_ip(const cpu_regs&)
{
    return 0;
}

void tep::set_ip(cpu_regs&, uintptr_t) {}

#endif // __x86_64__


#if defined(__x86_64__) || defined(__i386__)

void tep::rewind_trap(cpu_regs& regs)
{
    set_ip(regs, get_ip(regs) - 1);
}

#elif defined(__powerpc64__)

void tep::rewind_trap(cpu_regs& regs)
{
    set_ip(regs, get_ip(regs) - 4);
}

#else

void tep::rewind_trap(cpu_regs& regs) {}

#endif // __x86_64__ || __i386__


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
    // tw 31, 0, 0
    return (word & ~0xffffffff) | 0x7fe00008;
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

int tep::get_ptrace_opts(bool trace_children)
{
    // TODO: find a way to always set option PTRACE_O_EXITKILL
    // should be present since Linux 3.8 but not always defined in ptrace headers
#if !defined(PTRACE_O_EXITKILL)
#define PTRACE_O_EXITKILL (1 << 20)
#endif

    int opts = 0;
    // kill the tracee when the profiler errors
    opts = PTRACE_O_EXITKILL;
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