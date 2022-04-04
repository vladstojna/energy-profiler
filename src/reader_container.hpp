// reader_container.hpp

#pragma once

#include "configfwd.hpp"

#include <nrg/hybrid_reader.hpp>
#include <nrg/reader_gpu.hpp>
#include <nrg/reader_rapl.hpp>

namespace tep {
struct flags;

class reader_container {
private:
  nrgprf::reader_rapl _rdr_cpu;
  nrgprf::reader_gpu _rdr_gpu;
  std::vector<std::pair<cfg::target, nrgprf::hybrid_reader>> _hybrids;

public:
  reader_container(const flags &, const cfg::config_t &);
  ~reader_container();
  reader_container(const reader_container &);
  reader_container(reader_container &&);

  reader_container &operator=(reader_container &&);
  reader_container &operator=(const reader_container &);

  nrgprf::reader_rapl &reader_rapl();
  const nrgprf::reader_rapl &reader_rapl() const;

  nrgprf::reader_gpu &reader_gpu();
  const nrgprf::reader_gpu &reader_gpu() const;

  const nrgprf::reader *find(cfg::target) const;

private:
  template <bool Log = false> void emplace_hybrid_reader(cfg::target);
};
} // namespace tep
