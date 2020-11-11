// target.cpp
#include "target.h"
#include "util.h"
#include "macros.h"

#include <stdio.h>
#include <unistd.h>
#include <sys/personality.h>
#include <sys/ptrace.h>

#if !defined(ASLR)
#define ASLR 1
#endif

void tep::run_target(char* const argv[])
{
#if !defined(NDEBUG)
    tep::procmsg("running target:");
    for (size_t ix = 0; argv[ix] != NULL; ix++)
    {
        fprintf(stdout, " %s", argv[ix]);
    }
    fputc('\n', stdout);
#endif

    // set target process to be traced
    if (ptrace(PTRACE_TRACEME, 0, 0, 0) < 0)
    {
        perror(fileline("target ptrace"));
        return;
    }

#if !ASLR
    // disable ASLR for target process
    if (personality(ADDR_NO_RANDOMIZE) < 0)
    {
        // run again due to some kernel versions
        // being unable to return an error value
        if (personality(ADDR_NO_RANDOMIZE) < 0)
        {
            perror(fileline("personality"));
            return;
        }
    }
#endif

    // execute target executable
    if (execv(argv[0], argv) == -1)
    {
        perror(fileline("target execv"));
    }
}
