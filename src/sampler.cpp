// sampler.cpp

#include "sampler.hpp"
#include "log.hpp"

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
    return sampler_expected(nonstd::unexpect, nrgprf::errc::no_such_event);
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


sampler_promise short_sampler::run()&
{
    _start = nrgprf::timed_sample{};
    _start.timepoint(nrgprf::timed_sample::time_point::clock::now());
    if (std::error_code ec; !reader()->read(_start, ec))
    {
        auto promise = [ec]()
        {
            return sampler_expected(nonstd::unexpect, ec);
        };
        return promise;
    }
    return [this]()
    {
        return results();
    };
}

sampler_expected short_sampler::run()&&
{
    return std::move(*this).sampler::run();
}

sampler_expected short_sampler::results()
{
    _end = nrgprf::timed_sample{};
    _end.timepoint(nrgprf::timed_sample::time_point::clock::now());
    if (std::error_code ec; !reader()->read(_start, ec))
        return sampler_expected(nonstd::unexpect, ec);
    return timed_execution{ std::move(_start), std::move(_end) };
}


sync_sampler::sync_sampler(const nrgprf::reader* reader) :
    sampler(reader)
{}

sampler_expected sync_sampler::results()
{
    nrgprf::timed_sample s1;
    s1.timepoint(nrgprf::timed_sample::time_point::clock::now());
    if (std::error_code ec; !reader()->read(s1, ec))
    {
        log::logline(log::error, "%s: error when reading counters: %s",
            __func__, ec.message().c_str());
        return sampler_expected(nonstd::unexpect, ec);
    }

    work();

    nrgprf::timed_sample s2;
    s2.timepoint(nrgprf::timed_sample::time_point::clock::now());
    if (std::error_code ec; !reader()->read(s2, ec))
    {
        log::logline(log::error, "%s: error when reading counters: %s",
            __func__, ec.message().c_str());
        return sampler_expected(nonstd::unexpect, ec);
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
    return sampler_expected(nonstd::unexpect, nrgprf::errc::no_such_event);
}

sampler_expected null_async_sampler::results()
{
    return sampler_expected(nonstd::unexpect, nrgprf::errc::no_such_event);
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
    log::logline(log::debug, "%s: waiting to start", __func__);
    sig().wait();

    nrgprf::timed_sample first;
    first.timepoint(nrgprf::timed_sample::time_point::clock::now());
    if (std::error_code ec; !reader()->read(first, ec))
    {
        log::logline(log::error, "%s: error when reading counters: %s",
            __func__, ec.message().c_str());
        return sampler_expected(nonstd::unexpect, ec);
    }
    nrgprf::timed_sample last;
    while (!finished())
    {
        sig().wait_for(period());
        last.timepoint(nrgprf::timed_sample::time_point::clock::now());
        if (std::error_code ec; !reader()->read(last, ec))
        {
            log::logline(log::error, "%s: error when reading counters: %s",
                __func__, ec.message().c_str());
            return sampler_expected(nonstd::unexpect, ec);;
        }
    };
    log::logline(log::success, "%s: finished evaluation with %zu samples",
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
    timed_execution exec;
    exec.reserve(_initial_size);

    log::logline(log::debug, "%s: waiting to start", __func__);
    sig().wait();

    do
    {
        auto& smp = exec.emplace_back();
        smp.timepoint(nrgprf::timed_sample::time_point::clock::now());
        if (std::error_code ec; !reader()->read(smp, ec))
        {
            log::logline(log::error, "%s: error when reading counters: %s",
                __func__, ec.message().c_str());
            return sampler_expected(nonstd::unexpect, ec);;
        }
        sig().wait_for(period());
    } while (!finished());

    auto& smp = exec.emplace_back();
    smp.timepoint(nrgprf::timed_sample::time_point::clock::now());
    if (std::error_code ec; !reader()->read(smp, ec))
    {
        log::logline(log::error, "%s: error when reading counters: %s",
            __func__, ec.message().c_str());
        return sampler_expected(nonstd::unexpect, ec);;
    }

    log::logline(log::success, "%s: finished evaluation with %zu samples",
        __func__, exec.size());
    return exec;
}
