// periodic_sampler.hpp

#pragma once

#include "signaler.hpp"

#include <atomic>
#include <future>

#include <nrg/nrg.hpp>
#include <util/expected.hpp>

namespace tep
{

    class periodic_sampler
    {
    public:
        static struct simple_tag {} simple;
        static struct complete_tag {} complete;

    private:
        std::future<nrgprf::error> _future;
        nrgprf::execution _exec;
        signaler _sig;
        std::atomic_bool _finished;

    public:
        explicit periodic_sampler(nrgprf::execution&& exec = nrgprf::execution(0));

        explicit periodic_sampler(const nrgprf::reader* reader,
            nrgprf::execution&& exec,
            const std::chrono::milliseconds& period,
            complete_tag);

        explicit periodic_sampler(const nrgprf::reader* reader,
            nrgprf::execution&& exec,
            const std::chrono::milliseconds& period,
            simple_tag);

        ~periodic_sampler() noexcept;

        bool valid() const;

        void start();

        cmmn::expected<nrgprf::execution, nrgprf::error> get();

    private:
        nrgprf::error evaluate(const std::chrono::milliseconds& interval,
            const nrgprf::reader* reader);

        nrgprf::error evaluate_simple(const std::chrono::milliseconds& interval,
            const nrgprf::reader* reader);
    };

}