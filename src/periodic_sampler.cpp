// periodic_sampler.cpp

#include "periodic_sampler.hpp"
#include "util.hpp"

#include <cassert>
#include <unistd.h>

using namespace tep;


periodic_sampler::simple_tag periodic_sampler::simple;
periodic_sampler::complete_tag periodic_sampler::complete;


periodic_sampler::periodic_sampler() :
    _future(),
    _sig(false),
    _finished(false)
{}

periodic_sampler::periodic_sampler(const nrgprf::reader* reader,
    const std::chrono::milliseconds& period,
    complete_tag) :
    periodic_sampler()
{
    assert(reader != nullptr);
    _future = std::async(std::launch::async,
        [this](const std::chrono::milliseconds& period, const nrgprf::reader* reader)
        {
            return evaluate(period, reader);
        }, period, reader);
}

periodic_sampler::periodic_sampler(const nrgprf::reader* reader,
    const std::chrono::milliseconds& period,
    simple_tag) :
    periodic_sampler()
{
    assert(reader != nullptr);
    _future = std::async(std::launch::async,
        [this](const std::chrono::milliseconds& period, const nrgprf::reader* reader)
        {
            return evaluate_simple(period, reader);
        }, period, reader);
}

periodic_sampler::~periodic_sampler() noexcept
{
    if (!_future.valid())
        return;
    _finished = true;
    _sig.post();
    _future.wait();
}


bool periodic_sampler::valid() const
{
    return _future.valid();
}

void periodic_sampler::start()
{
    _sig.post();
}

cmmn::expected<timed_execution, nrgprf::error> periodic_sampler::results()
{
    _finished = true;
    _sig.post();
    return _future.get();
}

cmmn::expected<timed_execution, nrgprf::error>
periodic_sampler::evaluate(
    const std::chrono::milliseconds& interval,
    const nrgprf::reader* reader)
{
    assert(reader != nullptr);

    nrgprf::error error = nrgprf::error::success();
    timed_execution exec;
    log(log_lvl::debug, "%s: waiting to start", __func__);
    _sig.wait();
    do
    {
        exec.emplace_back(*reader, error);
        if (error)
        {
            // wait until section or target finishes
            log(log_lvl::error, "%s: error when reading counters: %s",
                __func__, error.msg().c_str());
            _sig.wait();
            return error;
        }
        _sig.wait_for(interval);
    } while (!_finished);

    exec.emplace_back(*reader, error);
    if (error)
        log(log_lvl::error, "%s: error when reading counters: %s",
            __func__, error.msg().c_str());

    log(log_lvl::success, "%s: finished evaluation with %zu samples",
        __func__, exec.size());
    return exec;
}

cmmn::expected<timed_execution, nrgprf::error>
periodic_sampler::evaluate_simple(
    const std::chrono::milliseconds& interval,
    const nrgprf::reader* reader)
{
    assert(reader != nullptr);

    nrgprf::error error = nrgprf::error::success();
    log(log_lvl::debug, "%s: waiting to start", __func__);
    _sig.wait();

    nrgprf::timed_sample first(*reader, error);
    nrgprf::timed_sample last = first;
    if (error)
    {
        // wait until section or target finishes
        log(log_lvl::error, "%s: error when reading counters: %s",
            __func__, error.msg().c_str());
        _sig.wait();
        return error;
    }
    while (!_finished)
    {
        _sig.wait_for(interval);
        last = nrgprf::timed_sample(*reader, error);
        if (error)
        {
            log(log_lvl::error, "%s: error when reading counters: %s",
                __func__, error.msg().c_str());
            _sig.wait();
            return error;
        }
    };
    log(log_lvl::success, "%s: finished evaluation with %zu samples",
        __func__, 2);
    return timed_execution({ std::move(first), std::move(last) });
}
