// sampler.cpp

#include "sampler.hpp"
#include "util.hpp"

#include <cassert>

using namespace tep;


sampler::sampler(const nrgprf::reader* r) :
    _reader(r)
{
    assert(_reader != nullptr);
}

const nrgprf::reader* sampler::reader() const
{
    return _reader;
}



sync_sampler::sync_sampler(const nrgprf::reader* reader, const std::function<void()>& work) :
    sampler(reader),
    _work(work)
{}

void sync_sampler::start()
{
    // No-op; synchronous samplers do all their work in results()
}

cmmn::expected<timed_execution, nrgprf::error> sync_sampler::results()
{
    nrgprf::error error = nrgprf::error::success();
    nrgprf::timed_sample s1(*reader(), error);
    if (error)
        return error;

    _work();

    nrgprf::timed_sample s2(*reader(), error);
    if (error)
        return error;
    return timed_execution{ std::move(s1), std::move(s2) };
}



decltype(async_sampler::_future)& async_sampler::ftr()
{
    return _future;
}

const decltype(async_sampler::_future)& async_sampler::ftr() const
{
    return _future;
}

async_sampler::async_sampler(const nrgprf::reader* r) :
    sampler(r),
    _future()
{}

async_sampler::~async_sampler()
{
    if (_future.valid())
        _future.wait();
}

bool async_sampler::valid() const
{
    return _future.valid();
}



std::chrono::milliseconds idle_sampler::default_sleep(5000);

idle_sampler::idle_sampler(std::unique_ptr<async_sampler>&& ptr,
    const std::chrono::milliseconds& sleep_for) :
    _sampler(std::move(ptr)),
    _sleep_for(sleep_for)
{}

void idle_sampler::start()
{
    // No-op; idle_sampler is synchronous
}

cmmn::expected<timed_execution, nrgprf::error> idle_sampler::results()
{
    _sampler->start();
    std::this_thread::sleep_for(_sleep_for);
    return _sampler->results();
}



periodic_sampler::periodic_sampler(const nrgprf::reader* r,
    const std::chrono::milliseconds& period) :
    async_sampler(r),
    _sig(false),
    _finished(false),
    _period(period)
{
    ftr() = std::async(std::launch::async, [this]()
        {
            return async_work();
        });
}

periodic_sampler::~periodic_sampler()
{
    if (ftr().valid())
    {
        _finished = true;
        _sig.post();
        ftr().wait();
    }
}

void periodic_sampler::start()
{
    _sig.post();
}

cmmn::expected<timed_execution, nrgprf::error> periodic_sampler::results()
{
    assert(!_finished && ftr().valid());
    if (_finished || !ftr().valid())
        return nrgprf::error(nrgprf::error_code::SETUP_ERROR,
            "Retrieving results without starting sampler");
    _finished = true;
    _sig.post();
    return ftr().get();
}

signaler& periodic_sampler::sig()
{
    return _sig;
}

const signaler& periodic_sampler::sig() const
{
    return _sig;
}

bool periodic_sampler::finished() const
{
    return _finished;
}

const std::chrono::milliseconds& periodic_sampler::period() const
{
    return _period;
}



std::chrono::milliseconds bounded_ps::default_period(30000); // 30 seconds

bounded_ps::bounded_ps(const nrgprf::reader* r, const std::chrono::milliseconds& p) :
    periodic_sampler(r, p)
{}

cmmn::expected<timed_execution, nrgprf::error> bounded_ps::async_work()
{
    nrgprf::error error = nrgprf::error::success();
    log(log_lvl::debug, "%s: waiting to start", __func__);
    sig().wait();

    nrgprf::timed_sample first(*reader(), error);
    nrgprf::timed_sample last = first;
    if (error)
    {
        // wait until section or target finishes
        log(log_lvl::error, "%s: error when reading counters: %s",
            __func__, error.msg().c_str());
        sig().wait();
        return error;
    }
    while (!finished())
    {
        sig().wait_for(period());
        last = nrgprf::timed_sample(*reader(), error);
        if (error)
        {
            log(log_lvl::error, "%s: error when reading counters: %s",
                __func__, error.msg().c_str());
            sig().wait();
            return error;
        }
    };
    log(log_lvl::success, "%s: finished evaluation with %zu samples",
        __func__, 2);
    return timed_execution{ std::move(first), std::move(last) };
}



std::chrono::milliseconds unbounded_ps::default_period(10); // 10 milliseconds

unbounded_ps::unbounded_ps(const nrgprf::reader* r, size_t initial_size,
    const std::chrono::milliseconds& period) :
    periodic_sampler(r, period),
    _initial_size(initial_size)
{}

cmmn::expected<timed_execution, nrgprf::error> unbounded_ps::async_work()
{
    nrgprf::error error = nrgprf::error::success();
    timed_execution exec;
    exec.reserve(_initial_size);

    log(log_lvl::debug, "%s: waiting to start", __func__);
    sig().wait();

    do
    {
        exec.emplace_back(*reader(), error);
        if (error)
        {
            // wait until section or target finishes
            log(log_lvl::error, "%s: error when reading counters: %s",
                __func__, error.msg().c_str());
            sig().wait();
            return error;
        }
        sig().wait_for(period());
    } while (!finished());

    exec.emplace_back(*reader(), error);
    if (error)
        log(log_lvl::error, "%s: error when reading counters: %s",
            __func__, error.msg().c_str());

    log(log_lvl::success, "%s: finished evaluation with %zu samples",
        __func__, exec.size());

    return exec;
}

// static timed_execution reserve_execution(const std::chrono::seconds& sleep,
//     const std::chrono::milliseconds& period)
// {
//     timed_execution exec;
//     uint32_t count = static_cast<uint32_t>(sleep / period) + 100;
//     exec.reserve(count);
//     log(log_lvl::debug, "reserved %zu samples", count);
//     return exec;
// }
