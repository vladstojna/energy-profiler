// target.cpp

#include "target.hpp"
#include "log.hpp"

#include <cerrno>
#include <cstring>
#include <sys/personality.h>
#include <sys/ptrace.h>
#include <unistd.h>

static int disable_aslr(pid_t pid) {
  using namespace tep;
  int old = personality(0xffffffff);
  if (old == -1) {
    log::logline(log::error, "[%d] error retrieving current persona: %s", pid,
                 strerror(errno));
    return old;
  }
  int result = personality(old | ADDR_NO_RANDOMIZE);
  if (result == -1) {
    log::logline(log::error, "[%d] error disabling ASLR: %s", pid,
                 strerror(errno));
    return result;
  }
  log::logline(log::success, "[%d] disabled ASLR", pid);
  return result;
}

void tep::run_target(bool aslr_randomization, char *const argv[]) {
  using namespace tep;
  pid_t pid = getpid();
  log::logline(log::info, "[%d] running target: %s", pid, argv[0]);
  for (size_t ix = 1; argv[ix] != NULL; ix++)
    log::logline(log::info, "[%d] argument %zu: %s", pid, ix, argv[ix]);
  // set target process to be traced
  if (ptrace(PTRACE_TRACEME, 0, 0, 0) == -1) {
    log::logline(log::error, "[%d] PTRACE_TRACEME: %s", pid, strerror(errno));
    return;
  }
  if (!aslr_randomization && disable_aslr(pid))
    return;
  log::flush();
  // execute target executable
  if (execvp(argv[0], argv) == -1)
    log::logline(log::error, "[%d] execv error: %s", pid, strerror(errno));
}
