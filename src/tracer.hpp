// tracer.hpp

#pragma once

#include <condition_variable>
#include <future>
#include <mutex>
#include <unordered_map>

#include <nrg/nrg.hpp>

#include "reader_container.hpp"
#include "error.hpp"
#include "sampler.hpp"
#include "trap.hpp"
#include "util.hpp"


namespace tep
{

    template<typename R>
    using tracer_expected = cmmn::expected<R, tracer_error>;

    class tracer
    {
    public:
        using gathered_results = std::unordered_map<addr_bounds,
            std::vector<sampler_expected>,
            addr_bounds_hash>;

    private:
        static std::mutex TRAP_BARRIER;

    private:
        std::future<tep::tracer_error> _tracer_ftr;

        mutable std::mutex _children_mx;
        std::vector<std::unique_ptr<tracer>> _children;
        const tracer* _parent;

        std::unique_ptr<async_sampler> _sampler;

        pid_t _tracee_tgid;
        pid_t _tracee;
        uintptr_t _ep;
        gathered_results _results;

    public:
        tracer(const registered_traps& traps,
            pid_t tracee_pid,
            pid_t tracee_tid,
            uintptr_t ep,
            std::launch policy);

        tracer(const registered_traps& traps,
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
        void add_child(const registered_traps& traps, pid_t new_child);

        tracer_error stop_tracees(const tracer& excl) const;
        tracer_error stop_self() const;
        tracer_error wait_for_tracee(int& wait_status) const;
        tracer_error handle_breakpoint(cpu_regs& regs, uintptr_t ep, long origw) const;
        tracer_error trace(const registered_traps* traps);
    };


    // operator overloads

    bool operator==(const tracer& lhs, const tracer& rhs);
    bool operator!=(const tracer& lhs, const tracer& rhs);

}
