// sampler.cpp

#include "sampler.hpp"
#include "log.hpp"

#include <cassert>

using namespace tep;

sampler_promise sampler_interface::run() & {
  return [this]() { return results(); };
}

sampler_expected sampler_interface::run() && { return run()(); }

sampler_expected null_sampler::results() {
  return sampler_expected(nonstd::unexpect, nrgprf::errc::no_such_event);
}

sampler::sampler(const nrgprf::reader *r) : _reader(r) {
  assert(_reader != nullptr);
}

const nrgprf::reader *sampler::reader() const { return _reader; }

sampler_promise short_sampler::run() & {
  _start.timestamp = timed_sample::clock::now();
  if (std::error_code ec; !reader()->read(_start, ec)) {
    auto promise = [ec]() { return sampler_expected(nonstd::unexpect, ec); };
    return promise;
  }
  return [this]() { return results(); };
}

sampler_expected short_sampler::run() && {
  return std::move(*this).sampler::run();
}

sampler_expected short_sampler::results() {
  _end.timestamp = timed_sample::clock::now();
  if (std::error_code ec; !reader()->read(_end, ec))
    return sampler_expected(nonstd::unexpect, ec);
  return timed_execution{std::move(_start), std::move(_end)};
}

sampler_expected sync_sampler::results() {
  timed_sample s1;
  s1.timestamp = timed_sample::clock::now();
  if (std::error_code ec; !reader()->read(s1, ec)) {
    log::logline(log::error, "%s: error when reading counters: %s", __func__,
                 ec.message().c_str());
    return sampler_expected(nonstd::unexpect, ec);
  }

  work();

  timed_sample s2;
  s2.timestamp = timed_sample::clock::now();
  if (std::error_code ec; !reader()->read(s2, ec)) {
    log::logline(log::error, "%s: error when reading counters: %s", __func__,
                 ec.message().c_str());
    return sampler_expected(nonstd::unexpect, ec);
  }
  return timed_execution{std::move(s1), std::move(s2)};
}

void sync_sampler_fn::work() const { _work(); }

async_sampler::async_sampler(const nrgprf::reader *r) : sampler(r), _future() {}

async_sampler::~async_sampler() {
  if (_future.valid())
    _future.wait();
}

bool async_sampler::valid() const { return _future.valid(); }

null_async_sampler::null_async_sampler() : async_sampler(nullptr) {}

sampler_expected null_async_sampler::async_work() {
  return sampler_expected(nonstd::unexpect, nrgprf::errc::no_such_event);
}

sampler_expected null_async_sampler::results() {
  return sampler_expected(nonstd::unexpect, nrgprf::errc::no_such_event);
}

sampler_expected async_sampler_fn::results() {
  auto promise = _sampler->run();
  _work();
  return promise();
}

periodic_sampler::periodic_sampler(const nrgprf::reader *r,
                                   const std::chrono::milliseconds &period)
    : async_sampler(r), _finished(false), _period(period), _sig(false) {
  _future = std::async(std::launch::async, [this]() {
    log::logline(log::debug, "periodic_sampler: waiting to start");
    _sig.wait();
    return async_work();
  });
}

periodic_sampler::~periodic_sampler() {
  if (_future.valid()) {
    _finished = true;
    _sig.post();
    _future.wait();
  }
}

sampler_promise periodic_sampler::run() & {
  _sig.post();
  return async_sampler::run();
}

sampler_expected periodic_sampler::run() && {
  return std::move(*this).async_sampler::run();
}

sampler_expected periodic_sampler::results() {
  assert(!_finished && _future.valid());
  _finished = true;
  _sig.post();
  return _future.get();
}

bool periodic_sampler::finished() const { return _finished; }

const std::chrono::milliseconds &periodic_sampler::period() const {
  return _period;
}

const std::chrono::milliseconds bounded_ps::default_period(30000);
const std::chrono::milliseconds unbounded_ps::default_period(10);
const size_t unbounded_ps::default_initial_size(384);

bounded_ps::bounded_ps(const nrgprf::reader *reader,
                       const std::chrono::milliseconds &period)
    : periodic_sampler(reader, period) {}

sampler_expected bounded_ps::async_work() {
  _first.timestamp = timed_sample::clock::now();
  if (std::error_code ec; !reader()->read(_first, ec)) {
    log::logline(log::error, "%s: error when reading counters: %s", __func__,
                 ec.message().c_str());
    return sampler_expected(nonstd::unexpect, ec);
  }
  while (!finished()) {
    _sig.wait_for(period());
    _last.timestamp = timed_sample::clock::now();
    if (std::error_code ec; !reader()->read(_last, ec)) {
      log::logline(log::error, "%s: error when reading counters: %s", __func__,
                   ec.message().c_str());
      return sampler_expected(nonstd::unexpect, ec);
      ;
    }
  };
  log::logline(log::success, "%s: finished evaluation with %zu samples",
               __func__, 2);
  return timed_execution{std::move(_first), std::move(_last)};
}

unbounded_ps::unbounded_ps(const nrgprf::reader *r, size_t initial_size,
                           const std::chrono::milliseconds &period)
    : periodic_sampler(r, period) {
  if (initial_size > 0)
    _exec.reserve(initial_size);
}

sampler_expected unbounded_ps::async_work() {
  do {
    auto &smp = _exec.emplace_back();
    smp.timestamp = timed_sample::clock::now();
    if (std::error_code ec; !reader()->read(smp, ec)) {
      log::logline(log::error, "%s: error when reading counters: %s", __func__,
                   ec.message().c_str());
      return sampler_expected(nonstd::unexpect, ec);
      ;
    }
    _sig.wait_for(period());
  } while (!finished());

  auto &smp = _exec.emplace_back();
  smp.timestamp = timed_sample::clock::now();
  if (std::error_code ec; !reader()->read(smp, ec)) {
    log::logline(log::error, "%s: error when reading counters: %s", __func__,
                 ec.message().c_str());
    return sampler_expected(nonstd::unexpect, ec);
    ;
  }

  log::logline(log::success, "%s: finished evaluation with %zu samples",
               __func__, _exec.size());
  return std::move(_exec);
}
