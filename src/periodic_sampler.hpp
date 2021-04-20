// periodic_sampler.hpp

#pragma once

#include "signaler.hpp"

#include <atomic>
#include <future>

#include <nrg.hpp>
#include <expected.hpp>

namespace tep
{

    class periodic_sampler
    {
    public:
        static struct simple_tag {} simple;
        static struct complete_tag {} complete;

    private:
        static std::chrono::milliseconds period_max;
        static std::chrono::milliseconds period_default;

    private:
        std::future<nrgprf::error> _future;
        nrgprf::execution _exec;
        signaler _sig;
        std::atomic_bool _finished;

    public:
        explicit periodic_sampler(nrgprf::execution&& exec = nrgprf::execution(0));

        explicit periodic_sampler(nrgprf::reader_rapl& reader, nrgprf::execution&& exec,
            complete_tag tag,
            const std::chrono::milliseconds& period = period_default);

        explicit periodic_sampler(nrgprf::reader_rapl& reader, nrgprf::execution&& exec,
            simple_tag tag,
            const std::chrono::milliseconds& period = period_max);

        explicit periodic_sampler(nrgprf::reader_gpu& reader, nrgprf::execution&& exec,
            complete_tag tag,
            const std::chrono::milliseconds& period = period_default);

        ~periodic_sampler() noexcept;

        bool valid() const;

        void start();

        cmmn::expected<nrgprf::execution, nrgprf::error> get();

    private:
        template<typename R>
        nrgprf::error evaluate(const std::chrono::milliseconds& interval, R* reader);

        template<typename R>
        nrgprf::error evaluate_simple(const std::chrono::milliseconds& interval, R* reader);
    };

}