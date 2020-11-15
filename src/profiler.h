// profiler.h
#pragma once

#include <cstdint>

#include <condition_variable>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <unordered_map>

namespace tep
{

class profiler_exception : public std::runtime_error
{
public:
    profiler_exception() :
        std::runtime_error("profiler finished unsuccessfully") {}
    profiler_exception(const std::string& what) :
        std::runtime_error(what) {}
    profiler_exception(const char* what) :
        std::runtime_error(what) {}
};

class profiler
{
private:
    std::thread _sampler_thread;
    mutable std::mutex _sampler_mutex;
    mutable std::condition_variable _sampler_cond;
    bool _task_finished;
    bool _target_finished;

    const pid_t _child_pid;
    const std::unordered_set<uintptr_t> _bp_addresses;

    uint64_t _trap_count;
    std::unordered_map<pid_t, bool> _children;
    bool _unsuccess;

public:
    profiler(pid_t child_pid,
        const std::chrono::milliseconds& interval,
        const std::unordered_set<uintptr_t>& addresses);
    profiler(pid_t child_pid,
        const std::chrono::milliseconds& interval,
        std::unordered_set<uintptr_t>&& addresses);
    ~profiler();

    // disable copying
    profiler(const profiler& other) = delete;
    profiler& operator=(const profiler& other) = delete;

    void run();

private:
    void sampler_routine(const std::chrono::milliseconds& interval);
    void notify_task();
    void notify_target_finished();
    bool signal_other_threads(pid_t tgid, pid_t caller_tid, int signal);
};

}