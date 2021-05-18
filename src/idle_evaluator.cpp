// idle_evaluator.cpp

#include "idle_evaluator.hpp"
#include "periodic_sampler.hpp"
#include "profiling_results.hpp"
#include "error.hpp"
#include "util.hpp"

#include <unistd.h>

using namespace tep;


static std::chrono::milliseconds period_simple(30000);
static std::chrono::milliseconds period_full(10);

static timed_execution reserve_execution(const std::chrono::seconds& sleep,
    const std::chrono::milliseconds& period)
{
    timed_execution exec;
    uint32_t count = static_cast<uint32_t>(sleep / period) + 100;
    exec.reserve(count);
    log(log_lvl::debug, "reserved %zu samples", count);
    return exec;
}

static timed_execution simple_execution()
{
    return {};
}

idle_evaluator::reserve_tag idle_evaluator::reserve;
std::chrono::seconds idle_evaluator::default_sleep_duration(5);

idle_evaluator::idle_evaluator(const nrgprf::reader* reader,
    const std::chrono::seconds& sleep_for) :
    _sleep(sleep_for),
    _sampler(reader, period_simple, periodic_sampler::simple)
{}

idle_evaluator::idle_evaluator(reserve_tag,
    const nrgprf::reader* reader,
    const std::chrono::seconds& sleep_for) :
    _sleep(sleep_for),
    _sampler(reader, period_full, periodic_sampler::complete)
{}


void idle_evaluator::idle()
{
    return std::this_thread::sleep_for(_sleep);
}

cmmn::expected<timed_execution, tracer_error> idle_evaluator::run()
{
    _sampler.start();
    idle();
    auto sampling_results = _sampler.results();
    if (!sampling_results)
        return tracer_error(tracer_errcode::READER_ERROR, sampling_results.error().msg());
    return std::move(sampling_results.value());
}
