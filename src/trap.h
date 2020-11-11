// trap.h
#pragma once

#if !defined(TRAP)
#define TRAP 1
#endif

#if TRAP == 0
#   include <stdio.h>
#   if defined(__linux__)
#       include <unistd.h>
#       include <sys/syscall.h>
#       if !defined(SYS_gettid)
#           include <errno.h>
#           define gettid() ((long)ENOSYS)
#       else
#           define gettid() ((long)syscall(SYS_gettid))
#       endif
#   endif
#endif

namespace tep::trap
{

__inline__ __attribute__((always_inline))
static void make()
{
#if TRAP && (__x86_64__ || __i386__)
    __asm__ volatile ("int $3");
#elif defined (__GNUC__)
#if defined(__linux__)
    printf("> would be trap @ 0x%016llx (tid = %ld)\n", (unsigned long long) __builtin_return_address(0), (long)gettid());
#else
    printf("> would be trap @ 0x%016llx\n", (unsigned long long) __builtin_return_address(0));
#endif
#else
#if defined(__linux__)
    printf("> would be trap (tid = %ld)\n", (long)gettid());
#else
    printf("> would be trap\n");
#endif
#endif
}

uint8_t code()
{
    return 0xCC;
}

}
