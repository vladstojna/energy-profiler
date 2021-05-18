// periodic_sampler.hpp

#pragma once

#include "signaler.hpp"

#include <atomic>
#include <future>
#include <vector>

#include <nrg/nrg.hpp>
#include <util/expected.hpp>

namespace tep
{

    using timed_execution = std::vector<nrgprf::timed_sample>;

    class periodic_sampler
    {
    public:
        static struct simple_tag {} simple;
        static struct complete_tag {} complete;

    private:
        std::future<cmmn::expected<timed_execution, nrgprf::error>> _future;
        signaler _sig;
        std::atomic_bool _finished;

        explicit periodic_sampler();

    public:
        explicit periodic_sampler(const nrgprf::reader* reader,
            const std::chrono::milliseconds& period,
            complete_tag);

        explicit periodic_sampler(const nrgprf::reader* reader,
            const std::chrono::milliseconds& period,
            simple_tag);

        ~periodic_sampler() noexcept;

        bool valid() const;

        void start();

        cmmn::expected<timed_execution, nrgprf::error> results();

    private:
        cmmn::expected<timed_execution, nrgprf::error> evaluate(
            const std::chrono::milliseconds& interval,
            const nrgprf::reader* reader);

        cmmn::expected<timed_execution, nrgprf::error> evaluate_simple(
            const std::chrono::milliseconds& interval,
            const nrgprf::reader* reader);
    };

}