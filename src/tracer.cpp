// tracer.cpp

#include "ptrace_wrapper.hpp"
#include "tracer.hpp"
#include "util.h"

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


void handle_error(pid_t tid, const char* comment, const nrgprf::error& e)
{
    std::stringstream sstream;
    sstream << e;
    log(log_lvl::error, "[%d] %s: error when reading counters: %s",
        tid, comment, sstream.str().c_str());
}


// end helper functions


trap_data::trap_data(const config_data::section& sec, long word) :
    section(sec),
    original_word(word)
{}


// definition of static variables


size_t tracer::DEFAULT_EXECS = 16;
size_t tracer::DEFAULT_SAMPLES = 256;
std::chrono::milliseconds tracer::DEFAULT_INTERVAL(30000);
std::mutex tracer::TRAP_BARRIER;


// methods


tracer::tracer(const std::unordered_map<uintptr_t, trap_data>& traps,
    pid_t tracee_pid, pid_t tracee_tid,
    const nrgprf::reader_rapl& rdr_cpu, const nrgprf::reader_gpu& rdr_gpu, std::launch policy) :
    tracer(traps, tracee_pid, tracee_tid, rdr_cpu, rdr_gpu, policy, nullptr)
{}

tracer::tracer(const std::unordered_map<uintptr_t, trap_data>& traps,
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
            std::vector<fallible_execution> entries = _results[addr];
            entries.insert(
                entries.end(),
                std::make_move_iterator(entry.begin()),
                std::make_move_iterator(entry.end()));
        }
    }
    return std::move(_results);
}


void tracer::add_child(const std::unordered_map<uintptr_t, trap_data>& traps, pid_t new_child)
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
    int tid = gettid();
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
    if (tgkill(_tracee_tgid, _tracee, SIGSTOP) != 0 && errno != ESRCH)
        return get_syserror(errno, tracer_errcode::SYSTEM_ERROR, gettid(), "tgkill");
    return tracer_error::success();
}


nrgprf::execution tracer::prepare_new_exec(const config_data::section& section) const
{
    int tid = gettid();
    nrgprf::execution exec(_exec.id() + 1);
    switch (section.method)
    {
    case config_data::profiling_method::energy_total:
    {
        exec.add(nrgprf::timepoint_t());
        exec.add(nrgprf::timepoint_t());
        log(log_lvl::debug, "[%d] added first and last sample to execution", tid);
    } break;
    case config_data::profiling_method::energy_profile:
    {
        size_t samples_reserved = section.samples == 0 ? DEFAULT_SAMPLES : section.samples;
        exec.reserve(samples_reserved);
        log(log_lvl::debug, "[%d] reserved %zu samples for execution", tid, samples_reserved);
    } break;
    }
    return exec;
}

void tracer::launch_async_sampling(const config_data::section& section)
{
    int tid = gettid();
    switch (section.target)
    {
    case config_data::target::cpu:
    {
        if (section.method == config_data::profiling_method::energy_profile)
            _sampler_ftr = std::async(std::launch::async, &tracer::evaluate_full_cpu, this,
                tid, section.interval, &_exec);
        else
            _sampler_ftr = std::async(std::launch::async, &tracer::evaluate_simple, this,
                tid, DEFAULT_INTERVAL, &_exec.first(), &_exec.last());
    } break;
    case config_data::target::gpu:
    {
        _sampler_ftr = std::async(std::launch::async, &tracer::evaluate_full_gpu, this,
            tid, section.interval, &_exec);
    } break;
    }
}


tracer_error tracer::trace(const std::unordered_map<uintptr_t, trap_data>* traps)
{
    assert(traps != nullptr);

    int wait_status;
    int tid = gettid();
    uintptr_t entrypoint = get_entrypoint_addr(_tracee_tgid);
    if (!entrypoint)
        return get_syserror(errno, tracer_errcode::SYSTEM_ERROR, tid, "get_entrypoint_addr");

    // TODO need to verify whether this is correct
    {
        std::scoped_lock lock(TRAP_BARRIER);
    }
    log(log_lvl::debug, "[%d] started tracer for tracee with tid %d, entrypoint @ 0x%" PRIxPTR,
        tid, _tracee, entrypoint);
    while (true)
    {
        int errnum;
        ptrace_wrapper& pw = ptrace_wrapper::instance;
        // continue the trace with our newly created tid
        if (pw.ptrace(errnum, PTRACE_CONT, _tracee, 0, 0) == -1)
            return get_syserror(errnum, tracer_errcode::PTRACE_ERROR, tid, "PTRACE_CONT");

        // wait for the exact tid of this thread
        pid_t waited_pid = waitpid(_tracee, &wait_status, 0);
        if (waited_pid == -1)
            return get_syserror(errno, tracer_errcode::SYSTEM_ERROR, tid, "waitpid");
        assert(waited_pid == _tracee);

        // if the tracee spawned a child
        if (tep::is_clone_event(wait_status) ||
            tep::is_vfork_event(wait_status) ||
            tep::is_fork_event(wait_status))
        {
            pid_t new_child;
            if (pw.ptrace(errnum, PTRACE_GETEVENTMSG, waited_pid, 0, &new_child) == -1)
                return get_syserror(errnum, tracer_errcode::PTRACE_ERROR, tid, "PTRACE_GETEVENTMSG");

            add_child(*traps, new_child);
        }
        // breakpoint reached
        else if (WIFSTOPPED(wait_status) && WSTOPSIG(wait_status) == SIGTRAP)
        {
            std::scoped_lock lock(TRAP_BARRIER);
            user_regs_struct regs;
            log(log_lvl::debug, "[%d] entered global tracer barrier", tid);

            tracer_error error = stop_tracees(*this);
            if (error)
                return error;

            if (pw.ptrace(errnum, PTRACE_GETREGS, waited_pid, 0, &regs) == -1)
                return get_syserror(errnum, tracer_errcode::PTRACE_ERROR, tid, "PTRACE_GETREGS");
            log(log_lvl::info, "[%d] reached breakpoint @ 0x%016llx (0x%llx)", tid,
                get_ip(regs), get_ip(regs) - entrypoint);

            // decrease the ip by 1 byte, since this is the size of the trap instruction
            set_ip(regs, get_ip(regs) - 1);
            uintptr_t start_bp_addr = get_ip(regs);

            const config_data::section& section = traps->at(start_bp_addr).section;
            _exec = prepare_new_exec(section);
            launch_async_sampling(section);

            // save the word with the trap byte
            long trap_word = pw.ptrace(errnum, PTRACE_PEEKDATA, _tracee, get_ip(regs), 0);
            if (errnum)
                return get_syserror(errnum, tracer_errcode::PTRACE_ERROR, tid, "PTRACE_PEEKDATA");
            log(log_lvl::debug, "[%d] peeked word @ 0x%016llx (0x%llx) with value 0x%016llx",
                tid, get_ip(regs), get_ip(regs) - entrypoint, trap_word);

            // set the registers and write the original word
            if (pw.ptrace(errnum, PTRACE_SETREGS, waited_pid, 0, &regs) == -1)
                return get_syserror(errnum, tracer_errcode::PTRACE_ERROR, tid, "PTRACE_SETREGS");
            if (pw.ptrace(errnum, PTRACE_POKEDATA, waited_pid, start_bp_addr,
                traps->at(start_bp_addr).original_word) == -1)
            {
                return get_syserror(errnum, tracer_errcode::PTRACE_ERROR, tid, "PTRACE_POKEDATA");
            }
            log(log_lvl::debug, "[%d] reset original word @ 0x%016llx (0x%llx), %016llx -> 0x%016llx",
                tid, get_ip(regs), get_ip(regs) - entrypoint,
                trap_word, traps->at(start_bp_addr).original_word);

            // single-step and reset the trap instruction
            if (pw.ptrace(errnum, PTRACE_SINGLESTEP, waited_pid, 0, 0) == -1)
                return get_syserror(errnum, tracer_errcode::PTRACE_ERROR, tid, "PTRACE_SINGLESTEP");

            // wait again for the single step
            waited_pid = waitpid(_tracee, &wait_status, 0);
            if (waited_pid == -1)
                return get_syserror(errno, tracer_errcode::SYSTEM_ERROR, tid, "waitpid");
            assert(waited_pid == _tracee);

            // if not SIGTRAP from the single-step
            // the process somehow ended unexpectedly
            if (!(WIFSTOPPED(wait_status) && WSTOPSIG(wait_status) == SIGTRAP))
            {
                log(log_lvl::error, "[%d] tried to single-step but process ended"
                    " unexpectedly and, as such, tracing cannot continue", tid);
                return { tracer_errcode::UNKNOWN_ERROR };
            }

            if (pw.ptrace(errnum, PTRACE_GETREGS, waited_pid, 0, &regs) == -1)
                return get_syserror(errnum, tracer_errcode::PTRACE_ERROR, tid, "PTRACE_GETREGS");
            log(log_lvl::info, "[%d] single-stepped @ 0x%016llx (0x%llx)", tid,
                get_ip(regs), get_ip(regs) - entrypoint);

            // reset the trap byte
            if (pw.ptrace(errnum, PTRACE_POKEDATA, waited_pid, start_bp_addr, trap_word) == -1)
                return get_syserror(errnum, tracer_errcode::PTRACE_ERROR, tid, "PTRACE_POKEDATA");
            log(log_lvl::debug, "[%d] reset trap word @ 0x%016llx (0x%llx), 0x%016llx -> 0x%016llx",
                tid, start_bp_addr, start_bp_addr - entrypoint, traps->at(start_bp_addr).original_word, trap_word);

            // notify sampling thread
            {
                log(log_lvl::debug, "[%d] notifying sampling thread to start", tid);
                std::unique_lock lock(_sampler_mtx);
                _section_finished = false;
            }
            _sampler_cnd.notify_one();

            // continue the tracee execution
            // it is during this time that the energy readings are done
            if (pw.ptrace(errnum, PTRACE_CONT, _tracee, 0, 0) == -1)
                return get_syserror(errnum, tracer_errcode::PTRACE_ERROR, tid, "PTRACE_CONT");

            pid_t waited_pid = waitpid(_tracee, &wait_status, 0);
            if (waited_pid == -1)
                return get_syserror(errno, tracer_errcode::SYSTEM_ERROR, tid, "waitpid");
            assert(waited_pid == _tracee);

            // reached end breakpoint
            if (WIFSTOPPED(wait_status) && WSTOPSIG(wait_status) == SIGTRAP)
            {
                // notify sampling thread that section has finished
                {
                    log(log_lvl::debug, "[%d] notifying sampling thread to finish", tid);
                    std::unique_lock lock(_sampler_mtx);
                    _section_finished = true;
                }
                _sampler_cnd.notify_one();
                nrgprf::error sampler_error = _sampler_ftr.get();
                // if sampling thread generated an error, register execution as a failed one
                // in the gathered results collection
                if (sampler_error)
                {
                    _results[start_bp_addr].emplace_back(sampler_error);
                    log(log_lvl::error, "[%d] sampling thread exited with error", tid);
                }
                else
                {
                    _results[start_bp_addr].emplace_back(_exec);
                    log(log_lvl::success, "[%d] sampling thread exited successfully with %zu samples",
                        tid, _exec.size());
                }

                // now proceed with the same behaviour as with any other breakpoint:
                // go back 1 instruction, replace the trap with the correct byte,
                // single-step, reset the trap and continue execution
                if (pw.ptrace(errnum, PTRACE_GETREGS, waited_pid, 0, &regs) == -1)
                    return get_syserror(errnum, tracer_errcode::PTRACE_ERROR, tid, "PTRACE_GETREGS");
                log(log_lvl::info, "[%d] reached breakpoint @ 0x%016llx (0x%llx)", tid,
                    get_ip(regs), get_ip(regs) - entrypoint);

                set_ip(regs, get_ip(regs) - 1);
                uintptr_t end_bp_addr = get_ip(regs);

                long trap_word = pw.ptrace(errnum, PTRACE_PEEKDATA, _tracee, get_ip(regs), 0);
                if (errnum)
                    return get_syserror(errnum, tracer_errcode::PTRACE_ERROR, tid, "PTRACE_PEEKDATA");
                log(log_lvl::debug, "[%d] peeked word @ 0x%016llx (0x%llx) with value 0x%016llx",
                    tid, get_ip(regs), get_ip(regs) - entrypoint, trap_word);

                // set the registers and write the original word
                if (pw.ptrace(errnum, PTRACE_SETREGS, waited_pid, 0, &regs) == -1)
                    return get_syserror(errnum, tracer_errcode::PTRACE_ERROR, tid, "PTRACE_SETREGS");
                if (pw.ptrace(errnum, PTRACE_POKEDATA, waited_pid, get_ip(regs),
                    traps->at(end_bp_addr).original_word) == -1)
                {
                    return get_syserror(errnum, tracer_errcode::PTRACE_ERROR, tid, "PTRACE_POKEDATA");
                }
                log(log_lvl::debug, "[%d] reset original word @ 0x%016llx (0x%llx), %016llx -> 0x%016llx",
                    tid, get_ip(regs), get_ip(regs) - entrypoint,
                    trap_word, traps->at(end_bp_addr).original_word);

                // single-step and reset the trap instruction
                if (pw.ptrace(errnum, PTRACE_SINGLESTEP, waited_pid, 0, 0) == -1)
                    return get_syserror(errnum, tracer_errcode::PTRACE_ERROR, tid, "PTRACE_SINGLESTEP");

                // wait again for the single step
                waited_pid = waitpid(_tracee, &wait_status, 0);
                if (waited_pid == -1)
                    return get_syserror(errno, tracer_errcode::SYSTEM_ERROR, tid, "waitpid");
                assert(waited_pid == _tracee);

                // if not SIGTRAP from the single-step
                // the process somehow ended unexpectedly
                if (!(WIFSTOPPED(wait_status) && WSTOPSIG(wait_status) == SIGTRAP))
                {
                    log(log_lvl::error, "[%d] tried to single-step but process ended"
                        " unexpectedly and, as such, tracing cannot continue", tid);
                    return { tracer_errcode::UNKNOWN_ERROR };
                }

                if (pw.ptrace(errnum, PTRACE_GETREGS, waited_pid, 0, &regs) == -1)
                    return get_syserror(errnum, tracer_errcode::PTRACE_ERROR, tid, "PTRACE_GETREGS");
                log(log_lvl::info, "[%d] single-stepped @ 0x%016llx (0x%llx)", tid,
                    get_ip(regs), get_ip(regs) - entrypoint);

                // reset the trap byte
                if (pw.ptrace(errnum, PTRACE_POKEDATA, waited_pid, end_bp_addr, trap_word) == -1)
                    return get_syserror(errnum, tracer_errcode::PTRACE_ERROR, tid, "PTRACE_POKEDATA");
                log(log_lvl::debug, "[%d] reset trap word @ 0x%016llx (0x%llx), %016llx -> 0x%016llx",
                    tid, end_bp_addr, end_bp_addr - entrypoint, traps->at(end_bp_addr).original_word, trap_word);
            }
            else
            {
                if (pw.ptrace(errnum, PTRACE_GETREGS, waited_pid, 0, &regs) == -1)
                    return get_syserror(errnum, tracer_errcode::PTRACE_ERROR, tid, "PTRACE_GETREGS");
                log(log_lvl::error, "[%d] received a signal mid-section: %s @ 0x%016llx", tid,
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
                strsignal(WTERMSIG(wait_status)));
            break;
        }
        else
        {
            user_regs_struct regs;
            if (pw.ptrace(errnum, PTRACE_GETREGS, waited_pid, 0, &regs) == -1)
                return get_syserror(errnum, tracer_errcode::PTRACE_ERROR, tid, "PTRACE_GETREGS");
            log(log_lvl::debug, "[%d] tracee %d received a signal: %s @ 0x%016llx", tid, _tracee,
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
