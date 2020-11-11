// util.cpp
#include "util.h"

#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>

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
