// ptrace_restarter.cpp

#include "ptrace_restarter.hpp"
#include "ptrace_wrapper.hpp"
#include "error.hpp"
#include "log.hpp"
#include "util.hpp"
#include "registers.hpp"

#include <cassert>
#include <cstring>
#include <sys/user.h>
#include <sys/wait.h>

namespace tep
{
    ptrace_restarter::ptrace_restarter(pid_t tid, pid_t tracee) noexcept :
        _tid(tid),
        _tracee(tracee)
    {}

    tracer_error ptrace_restarter::cont() noexcept
    {
        // sometimes PTRACE_CONT may fail with ESRCH despite the tracee existing
        // in /proc/<pid>/tasks, so the tracee is manually waited for if errno equals ESRCH
        // perhaps polling with WNOHANG and limited iterations would be better to avoid situations
        // where waiting is infinite
        int errnum;
        int wait_status;
        if (ptrace_wrapper::instance.ptrace(errnum, PTRACE_CONT, _tracee, 0, 0) == -1)
        {
            if (errnum != ESRCH)
                return get_syserror(errnum, tracer_errcode::PTRACE_ERROR, _tid, "PTRACE_CONT");

            log::logline(log::warning,
                "[%d] PTRACE_CONT failed with ESRCH: waiting for tracee %d",
                _tid, _tracee);
            pid_t waited_pid = waitpid(_tracee, &wait_status, 0);
            if (waited_pid == -1)
                return get_syserror(errno, tracer_errcode::SYSTEM_ERROR, _tid, "waitpid");
            assert(waited_pid == _tracee);

            cpu_gp_regs regs(_tracee);
            if (auto err = regs.getregs())
                return err;

            const char* sigstr = sig_str(WSTOPSIG(wait_status));
            log::logline(log::warning,
                "[%d] waited for tracee %d with signal: %s (status 0x%x),"
                " rip @ 0x%" PRIxPTR,
                _tid,
                _tracee,
                sigstr ? sigstr : "<no stop signal>",
                wait_status,
                regs.get_ip());

            if (ptrace_wrapper::instance.ptrace(errnum, PTRACE_CONT, _tracee, 0, 0) == -1)
                return get_syserror(errnum, tracer_errcode::PTRACE_ERROR, _tid, "PTRACE_CONT");
        }
        return tracer_error::success();
    }

    ptrace_restarter::~ptrace_restarter() noexcept
    {
        int errnum;
        if (!_tid)
            return;
        auto& pw = ptrace_wrapper::instance;
        if (pw.ptrace(errnum, PTRACE_DETACH, _tracee, 0, 0) == -1 && errnum != ESRCH)
            get_syserror(errnum, tracer_errcode::PTRACE_ERROR, _tid, "PTRACE_DETACH");
    }

    ptrace_restarter::ptrace_restarter(ptrace_restarter&& other) noexcept :
        _tid(std::exchange(other._tid, 0)),
        _tracee(other._tracee)
    {
        assert(other._tid == 0);
    }

    ptrace_restarter& ptrace_restarter::operator=(ptrace_restarter&& other) noexcept
    {
        _tid = std::exchange(other._tid, 0);
        _tracee = other._tracee;
        assert(other._tid == 0);
        return *this;
    }
}
