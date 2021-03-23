// util.cpp

#include "util.h"

#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <unistd.h>
#include <sys/user.h>


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

void tep::procmsg(const char* format, ...)
{
    va_list ap;
    fprintf(stdout, "[%d] ", getpid());
    va_start(ap, format);
    vfprintf(stdout, format, ap);
    va_end(ap);
}

uintptr_t tep::get_entrypoint_addr(pid_t pid)
{
    char filename[24];
    if (snprintf(filename, 24, "/proc/%d/maps", pid) >= 24)
        return 0;

    FILE* maps = fopen(filename, "r");
    if (maps == nullptr)
        return 0;

    uintptr_t ptr_start;
    int rv = fscanf(maps, "%" SCNxPTR, &ptr_start);
    if (rv == 0 || rv == EOF)
    {
        fclose(maps);
        return 0;
    }

    if (fclose(maps) != 0)
        return 0;

    return ptr_start;
}

uintptr_t tep::get_ip(const user_regs_struct& regs)
{
#if __x86_64__
    return regs.rip;
#elif __i386__
    return regs.eip;
#else // only x86 for now
    return 0;
#endif
}

void tep::set_ip(user_regs_struct& regs, uintptr_t addr)
{
#if __x86_64__
    regs.rip = addr;
#elif __i386__
    regs.eip = addr;
#else // only x86 for now
    // empty
#endif
}
