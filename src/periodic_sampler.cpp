// periodic_sampler.cpp

#include "periodic_sampler.hpp"
#include "util.hpp"

#include <cassert>
#include <unistd.h>

using namespace tep;


// begin helper functions

static void handle_reader_error(pid_t tid, const char* comment, const nrgprf::error& e)
{
    log(log_lvl::error, "[%d] %s: error when reading counters: %s",
        tid, comment, e.msg().c_str());
}

// end helper functions

periodic_sampler::simple_tag periodic_sampler::simple;
periodic_sampler::complete_tag periodic_sampler::complete;


periodic_sampler::periodic_sampler(nrgprf::execution&& exec) :
    _future(),
    _exec(std::move(exec)),
    _sig(false),
    _finished(false)
{}

periodic_sampler::periodic_sampler(const nrgprf::reader* reader,
    nrgprf::execution&& exec,
    const std::chrono::milliseconds& period,
    complete_tag) :
    periodic_sampler(std::move(exec))
{
    assert(reader != nullptr);
    _future = std::async(std::launch::async, &periodic_sampler::evaluate,
        this, period, reader);
}

periodic_sampler::periodic_sampler(const nrgprf::reader* reader,
    nrgprf::execution&& exec,
    const std::chrono::milliseconds& period,
    simple_tag) :
    periodic_sampler(std::move(exec))
{
    assert(reader != nullptr);
    _future = std::async(std::launch::async, &periodic_sampler::evaluate_simple,
        this, period, reader);
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

cmmn::expected<nrgprf::execution, nrgprf::error> periodic_sampler::get()
{
    _finished = true;
    _sig.post();
    nrgprf::error err = _future.get();
    if (err)
        return err;
    return std::move(_exec);
}

nrgprf::error periodic_sampler::evaluate(const std::chrono::milliseconds& interval,
    const nrgprf::reader* reader)
{
    assert(reader != nullptr);

    pid_t tid = gettid();
    log(log_lvl::debug, "[%d] %s: waiting to start", tid, __func__);
    _sig.wait();
    do
    {
        nrgprf::error error = reader->read(_exec.add(nrgprf::now()));
        if (error)
        {
            // wait until section or target finishes
            handle_reader_error(tid, __func__, error);
            _sig.wait();
            return error;
        }
        _sig.wait_for(interval);
    } while (!_finished);

    nrgprf::error error = reader->read(_exec.add(nrgprf::now()));
    if (error)
        handle_reader_error(tid, __func__, error);

    log(log_lvl::debug, "[%d] %s: finished evaluation", tid, __func__);
    return error;
}

nrgprf::error periodic_sampler::evaluate_simple(const std::chrono::milliseconds& interval,
    const nrgprf::reader* reader)
{
    assert(reader != nullptr);

    pid_t tid = gettid();
    nrgprf::sample& first = _exec.first();
    nrgprf::sample& last = _exec.last();
    log(log_lvl::debug, "[%d] %s: waiting to start", tid, __func__);
    _sig.wait();

    first.timepoint(nrgprf::now());
    nrgprf::error error = reader->read(first);
    if (error)
    {
        // wait until section or target finishes
        handle_reader_error(tid, __func__, error);
        _sig.wait();
        return error;
    }
    while (!_finished)
    {
        _sig.wait_for(interval);
        last.timepoint(nrgprf::now());
        error = reader->read(last);
        if (error)
        {
            handle_reader_error(tid, __func__, error);
            _sig.wait();
            return error;
        }
    };
    log(log_lvl::debug, "[%d] %s: finished evaluation", tid, __func__);
    return error;
}
