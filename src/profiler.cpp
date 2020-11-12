// profiler.cpp
#include "profiler.h"

#include <chrono>

#include <cassert>
#include <cinttypes>
#include <cstddef>
#include <cstdint>

#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/user.h>

#include "macros.h"
#include "trap.h"
#include "util.h"

// declarations

bool add_child(std::unordered_map<pid_t, bool>& children, pid_t pid);
bool insert_trap(pid_t pid, uintptr_t addr, long& word);

// start helper functions

constexpr uint32_t get_ptrace_opts()
{
    // kill the tracee when the profiler errors
    uint32_t opts = PTRACE_O_EXITKILL;
    // trace threads being spawned using clone()
    opts |= PTRACE_O_TRACECLONE;
    // trace children spawned with fork(): will most likely be useless
    opts |= PTRACE_O_TRACEFORK;
    // trace children spawned with vfork() i.e. clone() with CLONE_VFORK
    opts |= PTRACE_O_TRACEVFORK;

    return opts;
}

bool add_child(std::unordered_map<pid_t, bool>& children, pid_t pid)
{
    auto pair = children.try_emplace(pid, false);
    dbg(tep::procmsg("child %d created (total = %lu)\n", pid, children.size()));
    return pair.second;
}

bool insert_trap(pid_t pid, uintptr_t addr, long& word)
{
    // clear errno before call
    errno = 0;
    word = ptrace(PTRACE_PEEKDATA, pid, addr, 0);
    if (errno) // check if errno is set
    {
        perror(fileline("PTRACE_PEEKDATA"));
        return false;
    }
    long new_word = (word & tep::lsb_mask()) | tep::trap::code();
    if (ptrace(PTRACE_POKEDATA, pid, addr, new_word) < 0)
    {
        perror(fileline("PTRACE_POKEDATA"));
        return false;
    }
    return true;
}

// end helper functions

tep::profiler::profiler(pid_t child_pid, const std::unordered_set<uintptr_t>& addresses) :
    _sampler_thread(),
    _sampler_mutex(),
    _sampler_cond(),
    _task_finished(false),
    _target_finished(false),
    _child_pid(child_pid),
    _bp_addresses(addresses),
    _trap_count(0),
    _children()
{
    // start pooled sampling thread
    _sampler_thread = std::thread(&profiler::sampler_routine, this);
}

tep::profiler::profiler(pid_t child_pid, std::unordered_set<uintptr_t>&& addresses) :
    _sampler_thread(),
    _sampler_mutex(),
    _sampler_cond(),
    _task_finished(false),
    _target_finished(false),
    _child_pid(child_pid),
    _bp_addresses(std::move(addresses)),
    _trap_count(0),
    _children()
{
    // start pooled sampling thread
    _sampler_thread = std::thread(&profiler::sampler_routine, this);
}

tep::profiler::~profiler()
{
    notify_target_finished();
    _sampler_thread.join();

    for (size_t ix = 0; ix < _children.size(); ix++)
    {
        pid_t child_pid = wait(NULL);
        assert(child_pid != -1);
        dbg(tep::procmsg("waited for child %d\n", child_pid));
    }
}

void tep::profiler::notify_task()
{
    {
        std::scoped_lock lock(_sampler_mutex);
        _task_finished = bool(_trap_count++ % 2);
    }
    _sampler_cond.notify_one();
}

void tep::profiler::notify_target_finished()
{
    {
        std::scoped_lock lock(_sampler_mutex);
        _target_finished = true;
    }
    _sampler_cond.notify_one();
}

void tep::profiler::sampler_routine()
{
    while (true)
    {
        {
            std::unique_lock lock(_sampler_mutex);
            _sampler_cond.wait(lock);
            if (_target_finished)
                return;
            tep::procmsg("first sample\n");
        }
        {
            while (true)
            {
                std::unique_lock lock(_sampler_mutex);
                _sampler_cond.wait_for(lock, std::chrono::seconds(1),
                    [this] { return _task_finished || _target_finished; });
                tep::procmsg(_task_finished || _target_finished ?
                    "final sample\n" : "intermediate sample\n", false);
                if (_target_finished)
                    return;
                if (_task_finished)
                    break;
            }
        }
    }
}

bool tep::profiler::signal_other_threads(pid_t tgid, pid_t caller_tid, int signal)
{
    for (auto& [tid, stopped] : _children)
    {
        if (tid != caller_tid)
        {
            stopped = true;
            if (tgkill(tgid, tid, signal))
            {
                perror(fileline("tgkill"));
                return false;
            }
        }
    }
    return true;
}

bool tep::profiler::run()
{
    int wait_status;
    pid_t waited_pid;
    pid_t tgid;
    user_regs_struct regs;
    uint32_t ptrace_opts;
    uintptr_t entrypoint_addr;
    std::unordered_map<uintptr_t, long> original_words;

    // not necessary if running once: clear any children previously encountered
    _children.clear();
    ptrace_opts = get_ptrace_opts();

    // the thread group id is the global pid
    tgid = _child_pid;
    waited_pid = waitpid(_child_pid, &wait_status, 0);
    assert(waited_pid == _child_pid);
    if (!WIFSTOPPED(wait_status))
    {
        tep::procmsg(fileline("target not stopped\n"));
        return false;
    }
    ptrace(PTRACE_GETREGS, waited_pid, 0, &regs);
    dbg(tep::procmsg("target %d rip @ 0x%016llx\n", waited_pid, tep::get_ip(regs)));
    if ((entrypoint_addr = tep::get_entrypoint_addr(waited_pid)) == 0)
    {
        perror(fileline("tep::get_entrypoint_addr"));
        return false;
    }
    dbg(tep::procmsg("target %d entrypoint @ 0x%016llx\n", waited_pid, entrypoint_addr));
    ptrace(PTRACE_SETOPTIONS, waited_pid, NULL, ptrace_opts);
    if (!add_child(_children, waited_pid))
    {
        tep::procmsg("child %d already exists!", waited_pid);
        return false;
    }

    // set breakpoints
    for (uintptr_t addr : _bp_addresses)
    {
        long word;
        uintptr_t final_addr = entrypoint_addr + addr;
        // if trap insertion was successful & the address did not exist yet
        if (insert_trap(_child_pid, final_addr, word) &&
            original_words.try_emplace(final_addr, word).second)
        {
            dbg(tep::procmsg("inserted trap @ 0x%016llx\n", final_addr));
        }
        else
        {
            tep::procmsg(fileline("error setting breakpoints"));
            return false;
        }
    }

    // if no breakpoint addresses: measure the whole program
    if (_bp_addresses.empty())
    {
        notify_task();
    }

    // main tracing loop
    while (WIFSTOPPED(wait_status))
    {
        if (ptrace(PTRACE_CONT, waited_pid, 0, 0) < 0)
        {
            perror(fileline("PTRACE_CONT"));
            return false;
        }
        waited_pid = waitpid(-1, &wait_status, 0);
        if (tep::is_clone_event(wait_status) ||
            tep::is_vfork_event(wait_status) ||
            tep::is_fork_event(wait_status))
        {
            pid_t new_child;
            if (ptrace(PTRACE_GETEVENTMSG, waited_pid, 0, &new_child) < 0)
            {
                perror(fileline("PTRACE_GETEVENTMSG"));
                return false;
            }
            if (!add_child(_children, new_child))
            {
                tep::procmsg("child %d already exists!", new_child);
                return false;
            }
        }
        else if (WIFSTOPPED(wait_status) && WSTOPSIG(wait_status) == SIGTRAP)
        {
            ptrace(PTRACE_GETREGS, waited_pid, 0, &regs);
            tep::procmsg("child %d trapped @ 0x%016llx (0x%llx)\n", waited_pid,
                tep::get_ip(regs), tep::get_ip(regs) - entrypoint_addr);

            // rewind the PC 1 byte (trap instruction size)
            // no matter the thread and update the registers
            tep::set_ip(regs, tep::get_ip(regs) - 1);
            if (ptrace(PTRACE_SETREGS, waited_pid, 0, &regs) < 0)
            {
                perror(fileline("PTRACE_SETREGS"));
                return false;
            }
            // if child has not been marked as stopped then
            // it is the first thread to reach this code
            assert(_children.find(waited_pid) != _children.end());
            if (!_children.at(waited_pid))
            {
                if (!signal_other_threads(tgid, waited_pid, SIGSTOP))
                    return false;
                // replace the trap with the original word
                if (ptrace(PTRACE_POKEDATA, waited_pid, tep::get_ip(regs), original_words.at(tep::get_ip(regs))) < 0)
                {
                    perror(fileline("PTRACE_POKEDATA"));
                    return false;
                };
                notify_task();
            }
            // if child has been stopped, "unstop" it
            else
            {
                _children.at(waited_pid) = false;
            }
        }
        else if (WIFEXITED(wait_status))
        {
            _children.erase(waited_pid);
            tep::procmsg("child %d exited with status %d\n", waited_pid,
                WEXITSTATUS(wait_status));
        }
        else if (WIFSIGNALED(wait_status))
        {
            _children.erase(waited_pid);
            tep::procmsg("child %d terminated by signal: %s\n", waited_pid,
                strsignal(WTERMSIG(wait_status)));
        }
    #if !defined(NDEBUG)
        else
        {
            if (ptrace(PTRACE_GETREGS, waited_pid, 0, &regs) < 0)
            {
                perror(fileline("PTRACE_GETREGS"));
                return false;
            }
            tep::procmsg("child %d got a signal: %s @ 0x%016llx\n", waited_pid,
                strsignal(WSTOPSIG(wait_status)), tep::get_ip(regs));

        }
    #endif
    }
    return true;
}
