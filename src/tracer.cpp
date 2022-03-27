// tracer.cpp

#include "ptrace_restarter.hpp"
#include "ptrace_wrapper.hpp"
#include "ptrace_child_toggler.hpp"
#include "tracer.hpp"
#include "util.hpp"
#include "log.hpp"
#include "registers.hpp"
#include "trap.hpp"

#include <nonstd/expected.hpp>

#include <cassert>
#include <cstring>
#include <future>
#include <sstream>
#include <iostream>

#include <unistd.h>
#include <sys/syscall.h>
#include <sys/user.h>
#include <sys/wait.h>

using namespace tep;

// tgkill wrapper

inline int tgkill(pid_t tgid, pid_t tid, int signal)
{
    return syscall(SYS_tgkill, tgid, tid, signal);
}

// begin helper functions

template<typename T>
static std::string to_string(const T& obj)
{
    std::stringstream ss;
    ss << obj;
    return ss.str();
}

// end helper functions

// definition of static variables

std::mutex tracer::TRAP_BARRIER;

// methods

tracer::tracer(const registered_traps& traps,
    pid_t tracee_pid,
    pid_t tracee_tid,
    uintptr_t ep,
    std::launch policy) :
    tracer(traps, tracee_pid, tracee_tid, ep, policy, nullptr)
{}

tracer::tracer(const registered_traps& traps,
    pid_t tracee_pid,
    pid_t tracee_tid,
    uintptr_t ep,
    std::launch policy,
    const tracer* tracer) :
    _tracer_ftr(),
    _children_mx(),
    _children(),
    _parent(tracer),
    _tracee_tgid(tracee_pid),
    _tracee(tracee_tid),
    _ep(ep),
    _results()
{
    _tracer_ftr = std::async(policy, &tracer::trace, this, &traps);
}

tracer::~tracer()
{
    if (!_tracer_ftr.valid())
        return;
    _tracer_ftr.wait();
}

pid_t tracer::tracee() const
{
    return _tracee;
}


pid_t tracer::tracee_tgid() const
{
    return _tracee_tgid;
}


tracer_expected<tracer::gathered_results> tracer::results()
{
    using unexpected =
        tracer_expected<tracer::gathered_results>::unexpected_type;
    if (tracer_error error = _tracer_ftr.get())
        return unexpected{ std::move(error) };
    for (auto& child : _children)
    {
        tracer_expected<gathered_results> child_res = child->results();
        if (!child_res)
            return unexpected{ std::move(child_res.error()) };
        _results.insert(
            _results.end(),
            std::make_move_iterator(child_res->begin()),
            std::make_move_iterator(child_res->end()));
    }
    return std::move(_results);
}


void tracer::add_child(const registered_traps& traps, pid_t new_child)
{
    std::scoped_lock lock(_children_mx);
    _children.push_back(
        std::make_unique<tracer>(traps, _tracee_tgid, new_child, _ep,
            std::launch::async, this));
    log::logline(log::info, "[%d] new child created with tid=%d", gettid(), new_child);
}


tracer_error tracer::stop_tracees(const tracer& excl) const
{
    std::scoped_lock lock(_children_mx);
    pid_t tid = gettid();
    if (_parent != nullptr && *_parent != excl)
    {
        tracer_error error = _parent->stop_tracees(*this);
        if (error)
            return error;
        error = _parent->stop_self();
        if (error)
            return error;
        log::logline(log::info, "[%d] stopped parent %d", tid, _parent->tracee());
    }
    for (const auto& child : _children)
    {
        if (*child == excl)
            continue;
        tracer_error err = child->stop_tracees(*this);
        if (err)
            return err;
        err = child->stop_self();
        if (err)
            return err;
        log::logline(log::info, "[%d] stopped child %d", tid, child->tracee());
    }
    return tracer_error::success();
}

tracer_error tracer::stop_self() const
{
    if (tgkill(_tracee_tgid, _tracee, SIGSTOP) != 0)
    {
        if (errno == ESRCH)
            log::logline(log::warning, "[%d] tgkill: no process %d found but continuing anyway",
                gettid(), _tracee);
        else
            return get_syserror(errno, tracer_errcode::SYSTEM_ERROR, gettid(), "tgkill");
    }
    return tracer_error::success();
}

tracer_error tracer::wait_for_tracee(int& wait_status) const
{
    pid_t waited_pid = waitpid(_tracee, &wait_status, 0);
    if (waited_pid == -1)
        return get_syserror(errno, tracer_errcode::SYSTEM_ERROR, gettid(), "waitpid");
    assert(waited_pid == _tracee);
    return tracer_error::success();
}

tracer_error tracer::handle_breakpoint(cpu_gp_regs& regs, uintptr_t ep, long origw) const
{
    ptrace_wrapper& pw = ptrace_wrapper::instance;
    int errnum;
    int wait_status;
    pid_t tid = gettid();
    uintptr_t bp_addr = regs.get_ip();

    // save the word with the trap byte
    long trap_word = pw.ptrace(errnum, PTRACE_PEEKDATA, _tracee, bp_addr, 0);
    if (errnum)
        return get_syserror(errnum, tracer_errcode::PTRACE_ERROR, tid, "PTRACE_PEEKDATA");
    log::logline(log::debug, "[%d] peeked word @ 0x%" PRIxPTR " (0x%" PRIxPTR ") with value 0x%lx",
        tid, bp_addr, bp_addr - ep, trap_word);

    // set the registers and write the original word
    if (auto error = regs.setregs())
        return error;
    if (pw.ptrace(errnum, PTRACE_POKEDATA, _tracee, bp_addr, origw) == -1)
        return get_syserror(errnum, tracer_errcode::PTRACE_ERROR, tid, "PTRACE_POKEDATA");
    log::logline(log::debug, "[%d] reset original word @ 0x%" PRIxPTR " (0x%" PRIxPTR "), 0x%lx -> 0x%lx",
        tid, bp_addr, bp_addr - ep, trap_word, origw);

    // single-step and reset the trap instruction
    if (pw.ptrace(errnum, PTRACE_SINGLESTEP, _tracee, 0, 0) == -1)
        return get_syserror(errnum, tracer_errcode::PTRACE_ERROR, tid, "PTRACE_SINGLESTEP");
    if (auto error = wait_for_tracee(wait_status))
        return error;

    // if a SIGSTOP was queued up when multiple threads concurrently entered a section
    // singlestep again to suppress it
    if (WIFSTOPPED(wait_status) && WSTOPSIG(wait_status) == SIGSTOP)
    {
        log::logline(log::warning, "[%d] tracee %d stopped during single-step because of a SIGSTOP",
            tid, _tracee);
        if (pw.ptrace(errnum, PTRACE_SINGLESTEP, _tracee, 0, 0) == -1)
            return get_syserror(errnum, tracer_errcode::PTRACE_ERROR, tid, "PTRACE_SINGLESTEP");
        if (auto error = wait_for_tracee(wait_status))
            return error;
        cpu_gp_regs regs(_tracee);
        if (auto error = regs.getregs())
            return error;
        log::logline(log::warning, "[%d] SIGSTOP signal suppressed @ 0x%" PRIxPTR " (0x%" PRIxPTR ")", tid,
            regs.get_ip(), regs.get_ip() - ep);
    }

    if (!is_breakpoint_trap(wait_status))
    {
        log::logline(log::error, "[%d] tried to single-step but process ended"
            " unexpectedly and, as such, tracing cannot continue", tid);
        return tracer_error(tracer_errcode::UNKNOWN_ERROR);
    }

    if (auto error = regs.getregs())
        return error;
    log::logline(log::info, "[%d] single-stepped @ 0x%" PRIxPTR " (0x%" PRIxPTR ")", tid,
        regs.get_ip(), regs.get_ip() - ep);

    // reset the trap byte
    if (pw.ptrace(errnum, PTRACE_POKEDATA, _tracee, bp_addr, trap_word) == -1)
        return get_syserror(errnum, tracer_errcode::PTRACE_ERROR, tid, "PTRACE_POKEDATA");
    log::logline(log::debug, "[%d] reset trap word @ 0x%" PRIxPTR " (0x%" PRIxPTR "), 0x%lx -> 0x%lx",
        tid, bp_addr, bp_addr - ep, origw, trap_word);

    return tracer_error::success();
}

tracer_error tracer::trace(const registered_traps* traps)
{
    assert(traps != nullptr);

    int wait_status;
    pid_t tid = gettid();
    uintptr_t entrypoint = _ep;
    ptrace_wrapper& pw = ptrace_wrapper::instance;
    ptrace_restarter pr(tid, _tracee);

    log::logline(log::debug, "[%d] started tracer for tracee with tid %d, entrypoint @ 0x%" PRIxPTR,
        tid, _tracee, entrypoint);
    while (true)
    {
        if (auto err = pr.cont())
            return err;
        if (tracer_error error = wait_for_tracee(wait_status))
            return error;
        const char* sigstr = sig_str(WSTOPSIG(wait_status));
        log::logline(log::debug, "[%d] waited for tracee %d with signal: %s (status 0x%x)",
            tid, _tracee, sigstr ? sigstr : "<no stop signal>", wait_status);
        int errnum;
        if (is_child_event(wait_status))
        {
            std::scoped_lock lock(TRAP_BARRIER);
            unsigned long new_child;
            if (pw.ptrace(errnum, PTRACE_GETEVENTMSG, _tracee, 0, &new_child) == -1)
                return get_syserror(errnum, tracer_errcode::PTRACE_ERROR, tid, "PTRACE_GETEVENTMSG");

            add_child(*traps, static_cast<pid_t>(new_child));
        }
        else if (is_exit_event(wait_status))
        {
            std::scoped_lock lock(TRAP_BARRIER);
            unsigned long exit_status;
            if (pw.ptrace(errnum, PTRACE_GETEVENTMSG, _tracee, 0, &exit_status) == -1)
                return get_syserror(errnum, tracer_errcode::PTRACE_ERROR, tid, "PTRACE_GETEVENTMSG");
            log::logline(log::debug, "[%d] tracee %d PTRACE_O_TRACEEXIT status %d", tid, _tracee,
                static_cast<int>(exit_status));
        }
        else if (is_breakpoint_trap(wait_status))
        {
            cpu_gp_regs regs(_tracee);
            if (auto err = regs.getregs())
                return err;
            log::logline(log::info, "[%d] reached breakpoint @ 0x%" PRIxPTR " (0x%" PRIxPTR ")", tid,
                regs.get_ip(), regs.get_ip() - entrypoint);

            std::scoped_lock lock(TRAP_BARRIER);
            log::logline(log::debug, "[%d] entered global tracer barrier", tid);

            // disable tracing of children during execution of section
            auto toggler = ptrace_child_toggler::create(pw, tid, _tracee, false);
            if (!toggler)
                return std::move(toggler.error());
            log::logline(log::info, "[%d] child tracing disabled", tid);

            regs.rewind_trap();

            start_addr start_bp_addr = regs.get_ip();
            const start_trap* strap = traps->find(start_bp_addr);
            if (!strap)
            {
                log::logline(log::error, "[%d] reached start trap which is not registered as "
                    "a start trap @ 0x%" PRIxPTR " (offset = 0x%" PRIxPTR ")",
                    tid, start_bp_addr.val(), start_bp_addr.val() - entrypoint);
                return tracer_error(tracer_errcode::NO_TRAP, "No such trap registered");
            }
            log::logline(log::info, "[%d] reached starting trap located @ %s",
                tid, to_string(strap->context()).c_str());

            if (!strap->allow_concurrency())
            {
                log::logline(log::info, "[%d] concurrency not allowed; stopping tracees", tid);
                if (auto error = stop_tracees(*this))
                    return error;
            }
            else
                log::logline(log::info, "[%d] concurrency allowed; not stopping tracees", tid);

            if (auto error = handle_breakpoint(regs, entrypoint, strap->origword()))
                return error;
            _sampler = strap->create_sampler();
            sampler_promise _promise = _sampler->run();

            // it is during this time that the energy readings are done
            if (pw.ptrace(errnum, PTRACE_CONT, _tracee, 0, 0) == -1)
                return get_syserror(errnum, tracer_errcode::PTRACE_ERROR, tid, "PTRACE_CONT");

            if (auto error = wait_for_tracee(wait_status))
                return error;

            // reached end breakpoint
            if (is_breakpoint_trap(wait_status))
            {
                auto sampling_results = _promise();

                if (auto error = regs.getregs())
                    return error;
                log::logline(log::info, "[%d] reached breakpoint @ 0x%" PRIxPTR " (0x%" PRIxPTR ")", tid,
                    regs.get_ip(), regs.get_ip() - entrypoint);

                regs.rewind_trap();

                end_addr end_bp_addr = regs.get_ip();
                const end_trap* etrap = traps->find(end_bp_addr, start_bp_addr);
                if (!etrap)
                {
                    log::logline(log::error, "[%d] reached end trap @ 0x%" PRIxPTR
                        " (offset = 0x%" PRIxPTR ") which does not exist or is not registered as "
                        "an end trap for starting trap @ 0x%" PRIxPTR " (offset = 0x%" PRIxPTR ")",
                        tid, end_bp_addr.val(), end_bp_addr.val() - entrypoint,
                        start_bp_addr.val(), start_bp_addr.val() - entrypoint);
                    return tracer_error(tracer_errcode::NO_TRAP, "No such trap registered");
                }
                log::logline(log::info, "[%d] reached ending trap located @ %s",
                    tid, to_string(etrap->context()).c_str());

                // if sampling thread generated an error, register execution as a failed one
                // in the gathered results collection
                if (!sampling_results)
                    log::logline(log::error, "[%d] sampling thread exited with error", tid);
                else
                    log::logline(log::success, "[%d] sampling thread exited successfully with %zu samples",
                        tid, sampling_results->size());

                _results.push_back(
                    results_entry{
                        strap->context(),
                        etrap->context(),
                        std::move(sampling_results)
                    });

                if (auto error = handle_breakpoint(regs, entrypoint, etrap->origword()))
                    return error;
            }
            else
            {
                if (auto error = regs.getregs())
                    return error;
                log::logline(log::error, "[%d] received a signal mid-section: %s @ 0x%" PRIxPTR, tid,
                    strsignal(WSTOPSIG(wait_status)), regs.get_ip());
                return { tracer_errcode::SIGNAL_DURING_SECTION_ERROR,
                    "Tracee received signal during section execution" };
            }
            log::logline(log::info, "[%d] child tracing re-enabled", tid);
            log::logline(log::debug, "[%d] exited global tracer barrier", tid);
        }
        else if (WIFSTOPPED(wait_status) && WSTOPSIG(wait_status) == SIGSTOP)
        {
            // try to acquire barrier
            log::logline(log::info, "[%d] stopped tracee with tid=%d", tid, _tracee);
            std::scoped_lock lock(TRAP_BARRIER);
            log::logline(log::info, "[%d] continued tracee with tid=%d", tid, _tracee);
        }
        else if (WIFEXITED(wait_status))
        {
            log::logline(log::success, "[%d] tracee %d exited with status %d", tid, _tracee,
                WEXITSTATUS(wait_status));
            break;
        }
        else if (WIFSIGNALED(wait_status))
        {
            log::logline(log::success, "[%d] tracee %d signaled: %s", tid, _tracee,
                sig_str(WTERMSIG(wait_status)));
            break;
        }
        else
        {
            cpu_gp_regs regs(_tracee);
            if (tracer_error err = regs.getregs())
                return err;
            log::logline(log::debug, "[%d] tracee %d received a signal: %s @ 0x%" PRIxPTR, tid, _tracee,
                strsignal(WSTOPSIG(wait_status)), regs.get_ip());
        }
    }
    return tracer_error::success();
}


// operator overloads


bool tep::operator==(const tracer& lhs, const tracer& rhs)
{
    return lhs.tracee() == rhs.tracee();
}

bool tep::operator!=(const tracer& lhs, const tracer& rhs)
{
    return lhs.tracee() != rhs.tracee();
}
