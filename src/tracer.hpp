// tracer.hpp

#pragma once

#include <condition_variable>
#include <future>
#include <mutex>
#include <unordered_map>
#include <thread>

#include <nrg.hpp>

#include "config.hpp"
#include "error.hpp"

struct user_regs_struct;

namespace tep
{

    template<typename R>
    using tracer_expected = cmmn::expected<R, tracer_error>;

    using fallible_execution = cmmn::expected<nrgprf::execution, nrgprf::error>;

    using gathered_results = std::unordered_map<uintptr_t, std::vector<fallible_execution>>;


    struct trap_data
    {
        config_data::section section;
        long original_word;

        trap_data(const config_data::section& sec, long word);
    };


    class tracer
    {
    private:
        static size_t DEFAULT_EXECS;
        static size_t DEFAULT_SAMPLES;
        static std::chrono::milliseconds DEFAULT_INTERVAL;
        static std::mutex TRAP_BARRIER;

    private:
        std::future<tep::tracer_error> _tracer_ftr;
        std::future<nrgprf::error> _sampler_ftr;
        mutable std::mutex _sampler_mtx;
        mutable std::condition_variable _sampler_cnd;
        mutable bool _section_finished;

        mutable std::mutex _children_mx;
        std::vector<std::unique_ptr<tracer>> _children;
        const tracer* _parent;

        nrgprf::reader_rapl _rdr_cpu;
        nrgprf::reader_gpu _rdr_gpu;
        nrgprf::execution _exec;

        pid_t _tracee_tgid;
        pid_t _tracee;
        gathered_results _results;

    public:
        tracer(const std::unordered_map<uintptr_t, trap_data>& traps,
            pid_t tracee_pid, pid_t tracee_tid,
            const nrgprf::reader_rapl& rdr_cpu,
            const nrgprf::reader_gpu& rdr_gpu,
            std::launch policy);

        tracer(const std::unordered_map<uintptr_t, trap_data>& traps,
            pid_t tracee_pid, pid_t tracee_tid,
            const nrgprf::reader_rapl& rdr_cpu,
            const nrgprf::reader_gpu& rdr_gpu,
            std::launch policy, const tracer* parent);

        ~tracer();

        pid_t tracee() const;
        pid_t tracee_tgid() const;

        tracer_expected<gathered_results> results();

    private:
        void add_child(const std::unordered_map<uintptr_t, trap_data>& traps, pid_t new_child);
        tracer_error stop_tracees(const tracer& excl) const;
        tracer_error stop_self() const;
        tracer_error wait_for_tracee(int& wait_status) const;
        tracer_error handle_breakpoint(user_regs_struct& regs, uintptr_t ep, long origw) const;

        tracer_error trace(const std::unordered_map<uintptr_t, trap_data>* traps);

        nrgprf::execution prepare_new_exec(const config_data::section& section) const;
        void launch_async_sampling(const config_data::section& sec);

        void notify_start();
        void notify_end();
        void register_results(uintptr_t bp);

        nrgprf::error evaluate_full_gpu(pid_t tid,
            const std::chrono::milliseconds& interval,
            nrgprf::execution* execution);

        nrgprf::error evaluate_full_cpu(pid_t tid,
            const std::chrono::milliseconds& interval,
            nrgprf::execution* execution);

        nrgprf::error evaluate_simple(pid_t tid,
            const std::chrono::milliseconds& interval,
            nrgprf::sample* first,
            nrgprf::sample* last);
    };


    // operator overloads

    bool operator==(const tracer& lhs, const tracer& rhs);
    bool operator!=(const tracer& lhs, const tracer& rhs);

}
