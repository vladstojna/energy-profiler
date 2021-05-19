// sampler.hpp

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


    class sampler_interface
    {
    public:
        virtual ~sampler_interface() = default;
        virtual void start() = 0;
        virtual cmmn::expected<timed_execution, nrgprf::error> results() = 0;
    };


    class sampler : public sampler_interface
    {
    private:
        const nrgprf::reader* _reader;

    public:
        sampler(const nrgprf::reader*);

    protected:
        const nrgprf::reader* reader() const;
    };


    class sync_sampler final : public sampler
    {
    private:
        std::function<void()> _work;

    public:
        sync_sampler(const nrgprf::reader*, const std::function<void()>&);

        void start() override;
        cmmn::expected<timed_execution, nrgprf::error> results() override;
    };


    class async_sampler : public sampler
    {
    private:
        std::future<cmmn::expected<timed_execution, nrgprf::error>> _future;

    public:
        async_sampler(const nrgprf::reader*);
        ~async_sampler();

        bool valid() const;

    protected:
        decltype(_future)& ftr();
        const decltype(_future)& ftr() const;

        virtual cmmn::expected<timed_execution, nrgprf::error> async_work() = 0;
    };


    class idle_sampler : public sampler_interface
    {
    public:
        static std::chrono::milliseconds default_sleep;

    private:
        std::unique_ptr<async_sampler> _sampler;
        std::chrono::milliseconds _sleep_for;

    public:
        idle_sampler(std::unique_ptr<async_sampler>&&,
            const std::chrono::milliseconds& sleep_for = default_sleep);

        void start() override;
        cmmn::expected<timed_execution, nrgprf::error> results() override;
    };


    class periodic_sampler : public async_sampler
    {
    private:
        signaler _sig;
        std::atomic_bool _finished;
        std::chrono::milliseconds _period;

    public:
        periodic_sampler(const nrgprf::reader*, const std::chrono::milliseconds& period);
        ~periodic_sampler();

        void start() override;
        cmmn::expected<timed_execution, nrgprf::error> results() override;

        const std::chrono::milliseconds& period() const;

    protected:
        signaler& sig();
        const signaler& sig() const;

        bool finished() const;
    };


    class bounded_ps final : public periodic_sampler
    {
    public:
        static std::chrono::milliseconds default_period;

        bounded_ps(const nrgprf::reader*, const std::chrono::milliseconds& period = default_period);

    protected:
        cmmn::expected<timed_execution, nrgprf::error> async_work() override;
    };


    class unbounded_ps final : public periodic_sampler
    {
    private:
        size_t _initial_size;

    public:
        static std::chrono::milliseconds default_period;

        unbounded_ps(const nrgprf::reader*, size_t initial_size,
            const std::chrono::milliseconds& period = default_period);

    protected:
        cmmn::expected<timed_execution, nrgprf::error> async_work() override;
    };

}
