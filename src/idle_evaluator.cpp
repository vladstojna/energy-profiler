// idle_evaluator.cpp

#include "idle_evaluator.hpp"
#include "periodic_sampler.hpp"
#include "profiling_results.hpp"
#include "error.hpp"
#include "util.hpp"

#include <unistd.h>

using namespace tep;

static std::chrono::milliseconds cpu_interval(30000);
static std::chrono::milliseconds gpu_interval(10);


std::chrono::seconds idle_evaluator::default_sleep_duration(5);


idle_evaluator::idle_evaluator(const reader_container& readers,
    const std::chrono::seconds& sleep_for) :
    _readers(readers),
    _sleep(sleep_for)
{}


void idle_evaluator::idle()
{
    return std::this_thread::sleep_for(_sleep);
}


cmmn::expected<idle_results, tracer_error> idle_evaluator::run()
{
    pid_t tid = gettid();

    nrgprf::execution exec(0);
    exec.add(nrgprf::timepoint_t());
    exec.add(nrgprf::timepoint_t());

    std::unique_ptr<periodic_sampler> sampler =
        std::make_unique<periodic_sampler>(&_readers.reader_rapl(), std::move(exec),
            cpu_interval, periodic_sampler::simple);
    sampler->start();

    idle();

    cmmn::expected<nrgprf::execution, nrgprf::error> sampling_results = sampler->get();

    if (!sampling_results)
    {
        log(log_lvl::error, "[%d] unsuccessfuly gathered idle results for CPU: %s",
            tid, sampling_results.error().msg().c_str());
        return tracer_error(tracer_errcode::READER_ERROR, sampling_results.error().msg());
    }
    log(log_lvl::success, "[%d] successfuly gathered idle results for CPU", tid);

    // reserve enough samples to guarantee that no reallocation occurs
    uint32_t count = static_cast<uint32_t>(_sleep / gpu_interval) + 100;
    exec = nrgprf::execution(1);
    exec.reserve(count);
    log(log_lvl::debug, "[%d] reserved %" PRIu32 " samples for GPU evaluation", tid, count);

    sampler = std::make_unique<periodic_sampler>(&_readers.reader_gpu(), std::move(exec),
        gpu_interval, periodic_sampler::complete);
    sampler->start();

    idle();

    cmmn::expected<nrgprf::execution, nrgprf::error> sampling_results_gpu = sampler->get();
    if (!sampling_results_gpu)
    {
        log(log_lvl::error, "[%d] unsuccessfuly gathered idle results for GPU: %s",
            tid, sampling_results_gpu.error().msg().c_str());
        return tracer_error(tracer_errcode::READER_ERROR, sampling_results_gpu.error().msg());
    }
    log(log_lvl::success, "[%d] successfuly gathered idle results for GPU", tid);

    return { std::move(sampling_results.value()), std::move(sampling_results_gpu.value()) };
}
