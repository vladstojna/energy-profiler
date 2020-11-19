// util.cpp
#include "util.h"

#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/user.h>

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
        return 0;

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
