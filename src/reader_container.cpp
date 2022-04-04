// reader_container.cpp
#include "reader_container.hpp"
#include "config.hpp"
#include "error.hpp"
#include "flags.hpp"
#include "log.hpp"

#include <nonstd/expected.hpp>

#include <cassert>
#include <sstream>

using namespace tep;

static nrgprf::readings_type::type
effective_readings_type(nrgprf::readings_type::type rt) {
  return rt & nrgprf::readings_type::energy ? nrgprf::readings_type::energy
                                            : nrgprf::readings_type::power;
}

static nrgprf::reader_rapl
create_cpu_reader(const flags &flags,
                  const cfg::config_t::opt_params_t &params) {
  auto get_domain_mask = [&flags, &params]() {
    if (flags.locations.any())
      return flags.locations;
    if (!params || !params->domain_mask)
      return nrgprf::location_mask(~0x0);
    return nrgprf::location_mask(*params->domain_mask);
  };

  auto get_socket_mask = [&flags, &params]() {
    if (flags.sockets.any())
      return flags.sockets;
    if (!params || !params->socket_mask)
      return nrgprf::socket_mask(~0x0);
    return nrgprf::socket_mask(*params->socket_mask);
  };

  try {
    nrgprf::reader_rapl reader(get_domain_mask(), get_socket_mask(),
                               log::stream());
    log::logline(log::success, "created CPU reader");
    return reader;
  } catch (const nrgprf::exception &e) {
    log::logline(log::error, "%s: error creating CPU reader: %s", __func__,
                 e.what());
    throw;
  }
}

static nrgprf::reader_gpu
create_gpu_reader(const flags &flags,
                  const cfg::config_t::opt_params_t &params) {
  auto get_device_mask = [&flags, &params]() {
    if (flags.devices.any())
      return flags.devices;
    if (!params || !params->device_mask)
      return nrgprf::device_mask(~0x0);
    return nrgprf::device_mask(*params->device_mask);
  };

  auto devmask = get_device_mask();
  auto support = nrgprf::reader_gpu::support(devmask);
  if (!support)
    throw nrgprf::exception(support.error());
  try {
    nrgprf::reader_gpu reader(
        effective_readings_type(support ? *support
                                        : nrgprf::readings_type::all),
        devmask, log::stream());
    log::logline(log::success, "created GPU reader", "GPU");
    return reader;
  } catch (const nrgprf::exception &e) {
    log::logline(log::error, "%s: error creating GPU reader: %s", __func__,
                 e.what());
    throw;
  }
}

reader_container::reader_container(const flags &flags, const cfg::config_t &cd)
    : _rdr_cpu(create_cpu_reader(flags, cd.parameters())),
      _rdr_gpu(create_gpu_reader(flags, cd.parameters())) {
  for (const auto &g : cd.groups()) {
    for (const auto &s : g.sections) {
      assert(cfg::target_valid(s.targets));
      if (cfg::target_multiple(s.targets))
        emplace_hybrid_reader<true>(s.targets);
    }
  }
}

reader_container::~reader_container() = default;

reader_container::reader_container(const reader_container &other)
    : _rdr_cpu(other._rdr_cpu), _rdr_gpu(other._rdr_gpu), _hybrids() {
  _hybrids.reserve(other._hybrids.size());
  for (const auto &[tgts, hr] : other._hybrids)
    emplace_hybrid_reader(tgts);
}

reader_container &reader_container::operator=(const reader_container &other) {
  _rdr_cpu = other._rdr_cpu;
  _rdr_gpu = other._rdr_gpu;
  _hybrids.clear();
  _hybrids.reserve(other._hybrids.size());
  for (const auto &[tgts, hr] : other._hybrids)
    emplace_hybrid_reader(tgts);
  return *this;
}

reader_container::reader_container(reader_container &&other)
    : _rdr_cpu(std::move(other._rdr_cpu)), _rdr_gpu(std::move(other._rdr_gpu)),
      _hybrids() {
  _hybrids.reserve(other._hybrids.size());
  for (auto &[tgts, hr] : other._hybrids)
    emplace_hybrid_reader(std::move(tgts));
}

reader_container &reader_container::operator=(reader_container &&other) {
  _rdr_cpu = std::move(other._rdr_cpu);
  _rdr_gpu = std::move(other._rdr_gpu);
  _hybrids.clear();
  _hybrids.reserve(other._hybrids.size());
  for (auto &[tgts, hr] : other._hybrids)
    emplace_hybrid_reader(std::move(tgts));
  return *this;
}

nrgprf::reader_rapl &reader_container::reader_rapl() { return _rdr_cpu; }

const nrgprf::reader_rapl &reader_container::reader_rapl() const {
  return _rdr_cpu;
}

nrgprf::reader_gpu &reader_container::reader_gpu() { return _rdr_gpu; }

const nrgprf::reader_gpu &reader_container::reader_gpu() const {
  return _rdr_gpu;
}

const nrgprf::reader *reader_container::find(cfg::target target) const {
  if (target == cfg::target::cpu) {
    log::logline(log::debug, "retrieved RAPL reader");
    return &_rdr_cpu;
  }
  if (target == cfg::target::gpu) {
    log::logline(log::debug, "retrieved GPU reader");
    return &_rdr_gpu;
  }
  for (const auto &[tgt, hr] : _hybrids)
    if (tgt == target) {
      std::stringstream ss;
      ss << target;
      log::logline(log::debug, "retrieved hybrid reader for targets: %s",
                   ss.str().c_str());
      return &hr;
    }
  assert(false);
  return nullptr;
}

template <bool Log>
void reader_container::emplace_hybrid_reader(cfg::target targets) {
  auto &[tgts, hr] = _hybrids.emplace_back(targets, nrgprf::hybrid_reader{});
  for (cfg::target t = tgts, curr = cfg::target::cpu; cfg::target_valid(t);
       t &= ~curr, curr = cfg::target_next(curr)) {
    switch (t & curr) {
    case cfg::target::cpu:
      if constexpr (Log)
        log::logline(log::debug, "insert RAPL reader to hybrid");
      hr.push_back(_rdr_cpu);
      break;
    case cfg::target::gpu:
      if constexpr (Log)
        log::logline(log::debug, "insert GPU reader to hybrid");
      hr.push_back(_rdr_gpu);
      break;
    default:
      assert(false);
    }
  }
}
