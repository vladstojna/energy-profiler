// tracer.cpp

#include "ptrace_restarter.hpp"
#include "ptrace_wrapper.hpp"
#include "tracer.hpp"
#include "util.hpp"

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


// gettid wrapper

inline pid_t gettid()
{
    return syscall(SYS_gettid);
}


// begin helper functions


void handle_error(pid_t tid, const char* comment, const nrgprf::error& e)
{
    std::stringstream sstream;
    sstream << e;
    log(log_lvl::error, "[%d] %s: error when reading counters: %s",
        tid, comment, sstream.str().c_str());
}

tracer_expected<trap_set::iterator> get_trap(const trap_set& traps, pid_t tid, uintptr_t addr, uintptr_t ep)
{
    trap_set::iterator it = traps.find(addr);
    if (it == traps.end())
    {
        log(log_lvl::error, "[%d] reached trap which is not registered @ 0x%" PRIxPTR
            " (offset = 0x%" PRIxPTR ")", tid, addr, addr - ep);
        return tracer_error(tracer_errcode::NO_TRAP, "No such trap registered");
    }
    return it;
}


// end helper functions


// definition of static variables

size_t tracer::DEFAULT_EXECS = 16;
size_t tracer::DEFAULT_SAMPLES = 256;
std::chrono::milliseconds tracer::DEFAULT_INTERVAL(30000);
std::mutex tracer::TRAP_BARRIER;


// methods


tracer::tracer(const trap_set& traps,
    pid_t tracee_pid, pid_t tracee_tid,
    const nrgprf::reader_rapl& rdr_cpu, const nrgprf::reader_gpu& rdr_gpu, std::launch policy) :
    tracer(traps, tracee_pid, tracee_tid, rdr_cpu, rdr_gpu, policy, nullptr)
{}

tracer::tracer(const trap_set& traps,
    pid_t tracee_pid, pid_t tracee_tid,
    const nrgprf::reader_rapl& rdr_cpu, const nrgprf::reader_gpu& rdr_gpu, std::launch policy,
    const tracer* tracer) :
    _tracer_ftr(),
    _sampler_ftr(),
    _sampler_mtx(),
    _sampler_cnd(),
    _section_finished(false),
    _children_mx(),
    _children(),
    _parent(tracer),
    _rdr_cpu(rdr_cpu),
    _rdr_gpu(rdr_gpu),
    _exec(0),
    _tracee_tgid(tracee_pid),
    _tracee(tracee_tid),
    _results()
{
    _tracer_ftr = std::async(policy, &tracer::trace, this, &traps);
}

tracer::~tracer()
{
    std::scoped_lock lock(_children_mx);
    if (!_sampler_ftr.valid())
        return;
    {
        std::scoped_lock lock(_sampler_mtx);
        _section_finished = true;
    }
    _sampler_cnd.notify_one();
    _sampler_ftr.wait();
}

pid_t tracer::tracee() const
{
    return _tracee;
}


pid_t tracer::tracee_tgid() const
{
    return _tracee_tgid;
}


tracer_expected<gathered_results> tracer::results()
{
    tracer_error error = _tracer_ftr.get();
    if (error)
        return error;

    for (auto& child : _children)
    {
        tracer_expected<gathered_results> results = child->results();
        if (!results)
            return std::move(results.error());
        for (auto& [addr, entry] : results.value())
        {
            std::vector<fallible_execution>& entries = _results[addr];
            entries.insert(
                entries.end(),
                std::make_move_iterator(entry.begin()),
                std::make_move_iterator(entry.end()));
        }
    }
    return std::move(_results);
}


void tracer::add_child(const trap_set& traps, pid_t new_child)
{
    std::scoped_lock lock(_children_mx);
    _children.push_back(
        std::make_unique<tracer>(traps, _tracee_tgid, new_child,
            _rdr_cpu, _rdr_gpu, std::launch::async, this));
    log(log_lvl::info, "[%d] new child created with tid=%d", gettid(), new_child);
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
        log(log_lvl::info, "[%d] stopped parent %d", tid, _parent->tracee());
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
        log(log_lvl::info, "[%d] stopped child %d", tid, child->tracee());
    }
    return tracer_error::success();
}

tracer_error tracer::stop_self() const
{
    if (tgkill(_tracee_tgid, _tracee, SIGSTOP) != 0)
    {
        if (errno == ESRCH)
            log(log_lvl::warning, "[%d] tgkill: no process %d found but continuing anyway",
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

tracer_error tracer::handle_breakpoint(user_regs_struct& regs, uintptr_t ep, long origw) const
{
    ptrace_wrapper& pw = ptrace_wrapper::instance;
    int errnum;
    int wait_status;
    pid_t tid = gettid();
    uintptr_t bp_addr = get_ip(regs);

    // save the word with the trap byte
    long trap_word = pw.ptrace(errnum, PTRACE_PEEKDATA, _tracee, bp_addr, 0);
    if (errnum)
        return get_syserror(errnum, tracer_errcode::PTRACE_ERROR, tid, "PTRACE_PEEKDATA");
    log(log_lvl::debug, "[%d] peeked word @ 0x%" PRIxPTR " (0x%" PRIxPTR ") with value 0x%lx",
        tid, bp_addr, bp_addr - ep, trap_word);

    // set the registers and write the original word
    if (pw.ptrace(errnum, PTRACE_SETREGS, _tracee, 0, &regs) == -1)
        return get_syserror(errnum, tracer_errcode::PTRACE_ERROR, tid, "PTRACE_SETREGS");
    if (pw.ptrace(errnum, PTRACE_POKEDATA, _tracee, bp_addr, origw) == -1)
        return get_syserror(errnum, tracer_errcode::PTRACE_ERROR, tid, "PTRACE_POKEDATA");
    log(log_lvl::debug, "[%d] reset original word @ 0x%" PRIxPTR " (0x%" PRIxPTR "), 0x%lx -> 0x%lx",
        tid, bp_addr, bp_addr - ep, trap_word, origw);

    // single-step and reset the trap instruction
    if (pw.ptrace(errnum, PTRACE_SINGLESTEP, _tracee, 0, 0) == -1)
        return get_syserror(errnum, tracer_errcode::PTRACE_ERROR, tid, "PTRACE_SINGLESTEP");
    tracer_error werror = wait_for_tracee(wait_status);
    if (werror)
        return werror;

    // if a SIGSTOP was queued up when multiple threads concurrently entered a section
    // singlestep again to suppress it
    if (WIFSTOPPED(wait_status) && WSTOPSIG(wait_status) == SIGSTOP)
    {
        log(log_lvl::warning, "[%d] tracee %d stopped during single-step because of a SIGSTOP",
            tid, _tracee);
        if (pw.ptrace(errnum, PTRACE_SINGLESTEP, _tracee, 0, 0) == -1)
            return get_syserror(errnum, tracer_errcode::PTRACE_ERROR, tid, "PTRACE_SINGLESTEP");
        tracer_error werror = wait_for_tracee(wait_status);
        if (werror)
            return werror;
        user_regs_struct regs;
        if (pw.ptrace(errnum, PTRACE_GETREGS, _tracee, 0, &regs) == -1)
            return get_syserror(errnum, tracer_errcode::PTRACE_ERROR, tid, "PTRACE_GETREGS");
        log(log_lvl::warning, "[%d] SIGSTOP signal suppressed @ 0x%" PRIxPTR " (0x%" PRIxPTR ")", tid,
            get_ip(regs), get_ip(regs) - ep);
    }

    if (!is_breakpoint_trap(wait_status))
    {
        log(log_lvl::error, "[%d] tried to single-step but process ended"
            " unexpectedly and, as such, tracing cannot continue", tid);
        return tracer_error(tracer_errcode::UNKNOWN_ERROR);
    }

    if (pw.ptrace(errnum, PTRACE_GETREGS, _tracee, 0, &regs) == -1)
        return get_syserror(errnum, tracer_errcode::PTRACE_ERROR, tid, "PTRACE_GETREGS");
    log(log_lvl::info, "[%d] single-stepped @ 0x%" PRIxPTR " (0x%" PRIxPTR ")", tid,
        get_ip(regs), get_ip(regs) - ep);

    // reset the trap byte
    if (pw.ptrace(errnum, PTRACE_POKEDATA, _tracee, bp_addr, trap_word) == -1)
        return get_syserror(errnum, tracer_errcode::PTRACE_ERROR, tid, "PTRACE_POKEDATA");
    log(log_lvl::debug, "[%d] reset trap word @ 0x%" PRIxPTR " (0x%" PRIxPTR "), 0x%lx -> 0x%lx",
        tid, bp_addr, bp_addr - ep, origw, trap_word);

    return tracer_error::success();
}

nrgprf::execution tracer::prepare_new_exec(const config_data::section& section) const
{
    pid_t tid = gettid();
    nrgprf::execution exec(0);
    switch (section.method())
    {
    case config_data::profiling_method::energy_total:
    {
        exec.add(nrgprf::timepoint_t());
        exec.add(nrgprf::timepoint_t());
        log(log_lvl::debug, "[%d] added first and last sample to execution", tid);
    } break;
    case config_data::profiling_method::energy_profile:
    {
        size_t samples_reserved = section.samples() == 0 ? DEFAULT_SAMPLES : section.samples();
        exec.reserve(samples_reserved);
        log(log_lvl::debug, "[%d] reserved %zu samples for execution", tid, samples_reserved);
    } break;
    }
    return exec;
}

void tracer::launch_async_sampling(const config_data::section& section)
{
    pid_t tid = gettid();
    switch (section.target())
    {
    case config_data::target::cpu:
    {
        if (section.method() == config_data::profiling_method::energy_profile)
            _sampler_ftr = std::async(std::launch::async, &tracer::evaluate_full_cpu, this,
                tid, section.interval(), &_exec);
        else
            _sampler_ftr = std::async(std::launch::async, &tracer::evaluate_simple, this,
                tid, DEFAULT_INTERVAL, &_exec.first(), &_exec.last());
    } break;
    case config_data::target::gpu:
    {
        _sampler_ftr = std::async(std::launch::async, &tracer::evaluate_full_gpu, this,
            tid, section.interval(), &_exec);
    } break;
    }
}

void tracer::notify_start()
{
    {
        std::unique_lock lock(_sampler_mtx);
        _section_finished = false;
    }
    log(log_lvl::debug, "[%d] notified sampling thread to start", gettid());
    _sampler_cnd.notify_one();
}

void tracer::notify_end()
{
    {
        std::unique_lock lock(_sampler_mtx);
        _section_finished = true;
    }
    log(log_lvl::debug, "[%d] notified sampling thread to end", gettid());
    _sampler_cnd.notify_one();
}

void tracer::register_results(uintptr_t bp)
{
    pid_t tid = gettid();
    nrgprf::error sampler_error = _sampler_ftr.get();
    // if sampling thread generated an error, register execution as a failed one
    // in the gathered results collection
    if (sampler_error)
    {
        _results[bp].emplace_back(sampler_error);
        log(log_lvl::error, "[%d] sampling thread exited with error", tid);
    }
    else
    {
        size_t sz = _exec.size();
        _results[bp].emplace_back(std::move(_exec));
        log(log_lvl::success, "[%d] sampling thread exited successfully with %zu samples", tid, sz);
    }
}

tracer_error tracer::trace(const trap_set* traps)
{
    assert(traps != nullptr);

    int wait_status;
    pid_t tid = gettid();
    uintptr_t entrypoint = get_entrypoint_addr(_tracee_tgid);
    if (!entrypoint)
        return get_syserror(errno, tracer_errcode::SYSTEM_ERROR, tid, "get_entrypoint_addr");

    log(log_lvl::debug, "[%d] started tracer for tracee with tid %d, entrypoint @ 0x%" PRIxPTR,
        tid, _tracee, entrypoint);
    tracer_expected<ptrace_restarter> pr = ptrace_restarter::create();
    while (true)
    {
        int errnum;
        ptrace_wrapper& pw = ptrace_wrapper::instance;
        pr = ptrace_restarter::create(tid, _tracee, pw);
        if (!pr)
            return std::move(pr.error());

        tracer_error error = wait_for_tracee(wait_status);
        if (error)
            return error;
        log(log_lvl::debug, "[%d] waited for tracee %d with signal: %s (status 0x%x)", tid, _tracee,
            sig_str(WSTOPSIG(wait_status)), wait_status);

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
            log(log_lvl::debug, "[%d] tracee %d PTRACE_O_TRACEEXIT status %d", tid, _tracee,
                static_cast<int>(exit_status));
        }
        else if (is_breakpoint_trap(wait_status))
        {
            user_regs_struct regs;
            if (pw.ptrace(errnum, PTRACE_GETREGS, _tracee, 0, &regs) == -1)
                return get_syserror(errnum, tracer_errcode::PTRACE_ERROR, tid, "PTRACE_GETREGS");
            log(log_lvl::info, "[%d] reached breakpoint @ 0x%" PRIxPTR " (0x%" PRIxPTR ")", tid,
                get_ip(regs), get_ip(regs) - entrypoint);

            std::scoped_lock lock(TRAP_BARRIER);
            log(log_lvl::debug, "[%d] entered global tracer barrier", tid);

            tracer_error error = stop_tracees(*this);
            if (error)
                return error;

            // decrease the ip by 1 byte, since this is the size of the trap instruction
            set_ip(regs, get_ip(regs) - 1);
            uintptr_t start_bp_addr = get_ip(regs);
            tracer_expected<trap_set::iterator> trap =
                get_trap(*traps, tid, start_bp_addr, entrypoint);
            if (!trap)
                return std::move(trap.error());
            _exec = prepare_new_exec(trap.value()->section());
            launch_async_sampling(trap.value()->section());
            error = handle_breakpoint(regs, entrypoint, trap.value()->original_word());
            if (error)
                return error;
            notify_start();

            // it is during this time that the energy readings are done
            if (pw.ptrace(errnum, PTRACE_CONT, _tracee, 0, 0) == -1)
                return get_syserror(errnum, tracer_errcode::PTRACE_ERROR, tid, "PTRACE_CONT");

            error = wait_for_tracee(wait_status);
            if (error)
                return error;

            // reached end breakpoint
            if (is_breakpoint_trap(wait_status))
            {
                notify_end();
                register_results(start_bp_addr);

                if (pw.ptrace(errnum, PTRACE_GETREGS, _tracee, 0, &regs) == -1)
                    return get_syserror(errnum, tracer_errcode::PTRACE_ERROR, tid, "PTRACE_GETREGS");
                log(log_lvl::info, "[%d] reached breakpoint @ 0x%" PRIxPTR " (0x%" PRIxPTR ")", tid,
                    get_ip(regs), get_ip(regs) - entrypoint);

                set_ip(regs, get_ip(regs) - 1);
                uintptr_t end_bp_addr = get_ip(regs);
                tracer_expected<trap_set::iterator> trap =
                    get_trap(*traps, tid, end_bp_addr, entrypoint);
                if (!trap)
                    return std::move(trap.error());
                error = handle_breakpoint(regs, entrypoint, trap.value()->original_word());
                if (error)
                    return error;
            }
            else
            {
                if (pw.ptrace(errnum, PTRACE_GETREGS, _tracee, 0, &regs) == -1)
                    return get_syserror(errnum, tracer_errcode::PTRACE_ERROR, tid, "PTRACE_GETREGS");
                log(log_lvl::error, "[%d] received a signal mid-section: %s @ 0x%" PRIxPTR, tid,
                    strsignal(WSTOPSIG(wait_status)), get_ip(regs));
                return { tracer_errcode::SIGNAL_DURING_SECTION_ERROR,
                    "Tracee received signal during section execution" };
            }
            log(log_lvl::debug, "[%d] exited global tracer barrier", tid);
        }
        else if (WIFSTOPPED(wait_status) && WSTOPSIG(wait_status) == SIGSTOP)
        {
            // try to acquire barrier
            log(log_lvl::info, "[%d] stopped tracee with tid=%d", tid, _tracee);
            std::scoped_lock lock(TRAP_BARRIER);
            log(log_lvl::info, "[%d] continued tracee with tid=%d", tid, _tracee);
        }
        else if (WIFEXITED(wait_status))
        {
            log(log_lvl::success, "[%d] tracee %d exited with status %d", tid, _tracee,
                WEXITSTATUS(wait_status));
            break;
        }
        else if (WIFSIGNALED(wait_status))
        {
            log(log_lvl::success, "[%d] tracee %d exited with status %d", tid, _tracee,
                sig_str(WTERMSIG(wait_status)));
            break;
        }
        else
        {
            user_regs_struct regs;
            if (pw.ptrace(errnum, PTRACE_GETREGS, _tracee, 0, &regs) == -1)
                return get_syserror(errnum, tracer_errcode::PTRACE_ERROR, tid, "PTRACE_GETREGS");
            log(log_lvl::debug, "[%d] tracee %d received a signal: %s @ 0x%" PRIxPTR, tid, _tracee,
                strsignal(WSTOPSIG(wait_status)), get_ip(regs));
        }
    }
    return tracer_error::success();
}


// background thread routines


nrgprf::error tracer::evaluate_full_gpu(pid_t tid,
    const std::chrono::milliseconds& interval,
    nrgprf::execution* execution)
{
    assert(tid != 0);
    assert(execution != nullptr);

    std::unique_lock lock(_sampler_mtx);
    nrgprf::execution& exec = *execution;
    log(log_lvl::debug, "[%d] %s: waiting for the section to start", tid, __func__);
    _sampler_cnd.wait(lock);
    log(log_lvl::info, "[%d] %s: section started", tid, __func__);

    do
    {
        nrgprf::error error = _rdr_gpu.read(exec.add(nrgprf::now()));
        if (error)
        {
            // wait until section or target finishes
            handle_error(tid, __func__, error);
            _sampler_cnd.wait(lock);
            return error;
        }
        else
            _sampler_cnd.wait_for(lock, interval);
    } while (!_section_finished);

    nrgprf::error error = _rdr_gpu.read(exec.add(nrgprf::now()));
    if (error)
        handle_error(tid, __func__, error);
    return error;
}


nrgprf::error tracer::evaluate_full_cpu(pid_t tid,
    const std::chrono::milliseconds& interval,
    nrgprf::execution* execution)
{
    assert(tid != 0);
    assert(execution != nullptr);

    std::unique_lock lock(_sampler_mtx);
    nrgprf::execution& exec = *execution;
    log(log_lvl::debug, "[%d] %s: waiting for the section to start", tid, __func__);
    _sampler_cnd.wait(lock);
    log(log_lvl::info, "[%d] %s: section started", tid, __func__);

    do
    {
        nrgprf::error error = _rdr_cpu.read(exec.add(nrgprf::now()));
        if (error)
        {
            // wait until section or target finishes
            handle_error(tid, __func__, error);
            _sampler_cnd.wait(lock);
            return error;
        }
        _sampler_cnd.wait_for(lock, interval);
    } while (!_section_finished);

    nrgprf::error error = _rdr_cpu.read(exec.add(nrgprf::now()));
    if (error)
        handle_error(tid, __func__, error);
    return error;
}


nrgprf::error tracer::evaluate_simple(pid_t tid,
    const std::chrono::milliseconds& interval,
    nrgprf::sample* first,
    nrgprf::sample* last)
{
    assert(tid != 0);
    assert(first != nullptr);
    assert(last != nullptr);

    std::unique_lock lock(_sampler_mtx);
    log(log_lvl::debug, "[%d] %s: waiting for the section to start", tid, __func__);
    _sampler_cnd.wait(lock);
    log(log_lvl::info, "[%d] %s: section started", tid, __func__);

    first->timepoint(nrgprf::now());
    nrgprf::error error = _rdr_cpu.read(*first);
    if (error)
    {
        // wait until section or target finishes
        handle_error(tid, __func__, error);
        _sampler_cnd.wait(lock);
    }
    while (!_section_finished)
    {
        _sampler_cnd.wait_for(lock, interval);
        last->timepoint(nrgprf::now());
        error = _rdr_cpu.read(*last);
        if (error)
        {
            handle_error(tid, __func__, error);
            _sampler_cnd.wait(lock);
        }
    };
    return error;
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
