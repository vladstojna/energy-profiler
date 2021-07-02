// ptrace_restarter.cpp

#include "ptrace_restarter.hpp"
#include "ptrace_wrapper.hpp"
#include "error.hpp"
#include "util.hpp"

#include <cassert>
#include <cstring>
#include <sys/user.h>
#include <sys/wait.h>

#include <util/expected.hpp>

cmmn::expected<tep::ptrace_restarter, tep::tracer_error>
tep::ptrace_restarter::create(pid_t tid, pid_t tracee, ptrace_wrapper& pw)
{
    using namespace tep;
    tracer_error err = tracer_error::success();
    ptrace_restarter pr(tid, tracee, pw, err);
    if (err)
        return err;
    return pr;
}

cmmn::expected<tep::ptrace_restarter, tep::tracer_error>
tep::ptrace_restarter::create()
{
    return tracer_error::success();
}

tep::ptrace_restarter::ptrace_restarter(pid_t tid, pid_t tracee, ptrace_wrapper& pw, tracer_error& err) :
    _pw(pw),
    _tid(tid),
    _tracee(tracee)
{
    // sometimes PTRACE_CONT may fail with ESRCH despite the tracee existing
    // in /proc/<pid>/tasks, so the tracee is manually waited for if errno equals ESRCH
    // perhaps polling with WNOHANG and limited iterations would be better to avoid situations
    // where waiting is infinite
    int errnum;
    if (pw.ptrace(errnum, PTRACE_CONT, tracee, 0, 0) != -1)
        return;

    if (errnum != ESRCH)
    {
        err = get_syserror(errnum, tracer_errcode::PTRACE_ERROR, tid, "PTRACE_CONT");
        return;
    }

    log(log_lvl::warning, "[%d] PTRACE_CONT failed with ESRCH: waiting for tracee %d",
        tid, tracee);
    int wait_status;
    pid_t waited_pid = waitpid(tracee, &wait_status, 0);
    if (waited_pid == -1)
    {
        err = get_syserror(errno, tracer_errcode::SYSTEM_ERROR, tid, "waitpid");
        return;
    }
    assert(waited_pid == tracee);

    cpu_regs regs;
    if (pw.ptrace(errnum, PTRACE_GETREGS, tracee, 0, &regs) == -1)
    {
        err = get_syserror(errnum, tracer_errcode::PTRACE_ERROR, tid, "PTRACE_GETREGS");
        return;
    }

    log(log_lvl::warning, "[%d] waited for tracee %d with signal: %s (status 0x%x),"
        " rip @ 0x%" PRIxPTR, tid, tracee,
        sig_str(WSTOPSIG(wait_status)), wait_status, get_ip(regs));

    if (pw.ptrace(errnum, PTRACE_CONT, tracee, 0, 0) == -1)
    {
        err = get_syserror(errnum, tracer_errcode::PTRACE_ERROR, tid, "PTRACE_CONT");
        return;
    }
}

tep::ptrace_restarter::~ptrace_restarter()
{
    int errnum;
    if (!_tid)
        return;
    if (_pw.get().ptrace(errnum, PTRACE_DETACH, _tracee, 0, 0) == -1 && errnum != ESRCH)
        get_syserror(errnum, tracer_errcode::PTRACE_ERROR, _tid, "PTRACE_DETACH");
}

tep::ptrace_restarter::ptrace_restarter(ptrace_restarter&& other) :
    _pw(other._pw),
    _tid(std::exchange(other._tid, 0)),
    _tracee(other._tracee)
{
    assert(other._tid == 0);
}

tep::ptrace_restarter& tep::ptrace_restarter::operator=(ptrace_restarter&& other)
{
    _pw = other._pw;
    _tid = std::exchange(other._tid, 0);
    _tracee = other._tracee;
    assert(other._tid == 0);
    return *this;
}
