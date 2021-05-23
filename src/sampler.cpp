// sampler.cpp

#include "sampler.hpp"
#include "util.hpp"

#include <cassert>

using namespace tep;


sampler_promise sampler_interface::run()&
{
    return [this]()
    {
        return results();
    };
}

sampler_expected sampler_interface::run()&&
{
    return run()();
}



sampler_expected null_sampler::results()
{
    return nrgprf::error(nrgprf::error_code::NO_EVENT, "Null sampler results");
}



sampler::sampler(const nrgprf::reader* r) :
    _reader(r)
{
    assert(_reader != nullptr);
}

const nrgprf::reader* sampler::reader() const
{
    return _reader;
}



sync_sampler::sync_sampler(const nrgprf::reader* reader) :
    sampler(reader)
{}

sampler_expected sync_sampler::results()
{
    nrgprf::error error = nrgprf::error::success();
    nrgprf::timed_sample s1(*reader(), error);
    if (error)
    {
        log(log_lvl::error, "%s: error when reading counters: %s",
            __func__, error.msg().c_str());
        return error;
    }

    work();

    nrgprf::timed_sample s2(*reader(), error);
    if (error)
    {
        log(log_lvl::error, "%s: error when reading counters: %s",
            __func__, error.msg().c_str());
        return error;
    }
    return timed_execution{ std::move(s1), std::move(s2) };
}



void sync_sampler_fn::work() const
{
    _work();
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

decltype(async_sampler::_future)& async_sampler::ftr()
{
    return _future;
}

const decltype(async_sampler::_future)& async_sampler::ftr() const
{
    return _future;
}

void async_sampler::ftr(decltype(_future) && ftr)
{
    _future = std::move(ftr);
}


null_async_sampler::null_async_sampler() :
    async_sampler(nullptr)
{}

sampler_expected null_async_sampler::async_work()
{
    return nrgprf::error(nrgprf::error_code::NO_EVENT, "Async null sampler results");
}

sampler_expected null_async_sampler::results()
{
    return nrgprf::error(nrgprf::error_code::NO_EVENT, "Async null sampler results");
}



sampler_expected async_sampler_fn::results()
{
    auto promise = _sampler->run();
    _work();
    return promise();
}



periodic_sampler::periodic_sampler(const nrgprf::reader* r,
    const std::chrono::milliseconds& period) :
    async_sampler(r),
    _sig(false),
    _finished(false),
    _period(period)
{
    ftr(std::async(std::launch::async, [this]()
        {
            return async_work();
        }));
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

sampler_promise periodic_sampler::run()&
{
    _sig.post();
    return async_sampler::run();
}

sampler_expected periodic_sampler::run()&&
{
    return std::move(*this).async_sampler::run();
}

sampler_expected periodic_sampler::results()
{
    assert(!_finished && ftr().valid());
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

sampler_expected bounded_ps::async_work()
{
    nrgprf::error error = nrgprf::error::success();
    log(log_lvl::debug, "%s: waiting to start", __func__);
    sig().wait();

    nrgprf::timed_sample first(*reader(), error);
    nrgprf::timed_sample last = first;
    if (error)
    {
        log(log_lvl::error, "%s: error when reading counters: %s",
            __func__, error.msg().c_str());
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

sampler_expected unbounded_ps::async_work()
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
            log(log_lvl::error, "%s: error when reading counters: %s",
                __func__, error.msg().c_str());
            return error;
        }
        sig().wait_for(period());
    } while (!finished());

    exec.emplace_back(*reader(), error);
    if (error)
    {
        log(log_lvl::error, "%s: error when reading counters: %s",
            __func__, error.msg().c_str());
        return error;
    }

    log(log_lvl::success, "%s: finished evaluation with %zu samples",
        __func__, exec.size());

    return exec;
}
