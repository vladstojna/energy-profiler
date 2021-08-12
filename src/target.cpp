// target.cpp

#include "target.hpp"
#include "log.hpp"

#include <cstring>
#include <cerrno>
#include <unistd.h>
#include <sys/ptrace.h>

#if defined(NO_ASLR)

#include <sys/personality.h>

int disable_aslr(pid_t pid)
{
    int old = personality(0xffffffff);
    if (old == -1)
    {
        tep::log(tep::log_lvl::error, "[%d] error retrieving current persona: %s", pid, strerror(errno));
        return old;
    }
    int result = personality(old | ADDR_NO_RANDOMIZE);
    if (result == -1)
    {
        tep::log(tep::log_lvl::error, "[%d] error disabling ASLR: %s", pid, strerror(errno));
        return result;
    }
    tep::log(tep::log_lvl::success, "[%d] disabled ASLR", pid);
    return result;
}

#else

int disable_aslr(pid_t pid)
{
    (void)pid;
    return 0;
}

#endif

void tep::run_target(char* const argv[])
{
    using namespace tep;
    pid_t pid = getpid();
    log::logline(log::info, "[%d] running target: %s", pid, argv[0]);
    for (size_t ix = 1; argv[ix] != NULL; ix++)
        log::logline(log::info, "[%d] argument %zu: %s", pid, ix, argv[ix]);
    // set target process to be traced
    if (ptrace(PTRACE_TRACEME, 0, 0, 0) == -1)
    {
        log::logline(log::error, "[%d] PTRACE_TRACEME: %s", pid, strerror(errno));
        return;
    }
    if (disable_aslr(pid))
        return;
    // execute target executable
    if (execv(argv[0], argv) == -1)
        log::logline(log::error, "[%d] execv error: %s", pid, strerror(errno));
}
