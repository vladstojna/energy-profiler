// tracer.hpp

#pragma once

#include <condition_variable>
#include <future>
#include <mutex>
#include <unordered_map>

#include <nrg/nrg.hpp>

#include "reader_container.hpp"
#include "config.hpp"
#include "error.hpp"
#include "periodic_sampler.hpp"
#include "trap.hpp"

struct user_regs_struct;

namespace tep
{

    template<typename R>
    using tracer_expected = cmmn::expected<R, tracer_error>;

    using fallible_execution = cmmn::expected<timed_execution, nrgprf::error>;

    using gathered_results = std::unordered_map<uintptr_t, std::vector<fallible_execution>>;

    class tracer
    {
    private:
        static std::mutex TRAP_BARRIER;

    private:
        std::future<tep::tracer_error> _tracer_ftr;

        mutable std::mutex _children_mx;
        std::vector<std::unique_ptr<tracer>> _children;
        const tracer* _parent;

        reader_container _readers;
        std::unique_ptr<periodic_sampler> _sampler;

        pid_t _tracee_tgid;
        pid_t _tracee;
        uintptr_t _ep;
        gathered_results _results;

    public:
        tracer(const reader_container& readers,
            const trap_set& traps,
            pid_t tracee_pid,
            pid_t tracee_tid,
            uintptr_t ep,
            std::launch policy);

        tracer(const reader_container& readers,
            const trap_set& traps,
            pid_t tracee_pid,
            pid_t tracee_tid,
            uintptr_t ep,
            std::launch policy,
            const tracer* parent);

        ~tracer();

        pid_t tracee() const;
        pid_t tracee_tgid() const;

        tracer_expected<gathered_results> results();

    private:
        void add_child(const trap_set& traps, pid_t new_child);
        tracer_error stop_tracees(const tracer& excl) const;
        tracer_error stop_self() const;
        tracer_error wait_for_tracee(int& wait_status) const;
        tracer_error handle_breakpoint(user_regs_struct& regs, uintptr_t ep, long origw) const;

        tracer_error trace(const trap_set* traps);

        timed_execution prepare_new_exec(const config_data::section& section) const;
        void launch_async_sampling(const config_data::section& sec);
        void register_results(uintptr_t bp);
    };


    // operator overloads

    bool operator==(const tracer& lhs, const tracer& rhs);
    bool operator!=(const tracer& lhs, const tracer& rhs);

}
