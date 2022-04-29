// profiler.hpp

#pragma once

#include "config.hpp"
#include "dbg/object_info.hpp"
#include "flags.hpp"
#include "output.hpp"
#include "reader_container.hpp"
#include "trap.hpp"

#include <util/expectedfwd.hpp>

namespace tep {
class profiling_results;
class tracer_error;

class profiler {
private:
  struct output_mapping {
    using distance_pair =
        std::pair<std::iterator_traits<
                      profiling_results::container::iterator>::difference_type,
                  std::iterator_traits<
                      group_output::container::iterator>::difference_type>;
    using map_type =
        std::unordered_map<start_addr, distance_pair, start_addr::hash>;

    map_type map;
    profiling_results results;

    output_mapping() = default;

    bool insert(start_addr, const reader_container &, const cfg::group_t &,
                const cfg::section_t &);

    section_output *find(start_addr);
  };

  pid_t _tid;
  pid_t _child;
  flags _flags;
  dbg::object_info _dli;
  cfg::config_t _cd;
  reader_container _readers;
  registered_traps _traps;
  output_mapping _output;

public:
  profiler(pid_t child, flags, dbg::object_info, cfg::config_t);

  const dbg::object_info &debug_line_info() const;
  const cfg::config_t &config() const;
  const registered_traps &traps() const;

  tracer_error await_executable(const std::string &name) const;
  nonstd::expected<profiling_results, tracer_error> run();

private:
  tracer_error obtain_idle_results();

  tracer_error insert_traps_function(const cfg::group_t &,
                                     const cfg::section_t &,
                                     const cfg::function_t &, uintptr_t);

  tracer_error insert_traps_address_range(const cfg::group_t &,
                                          const cfg::section_t &,
                                          const cfg::address_range_t &,
                                          uintptr_t);

  nonstd::expected<start_addr, tracer_error>
  insert_traps_position_start(const cfg::section_t &, const cfg::position_t &,
                              uintptr_t);

  tracer_error insert_traps_position_end(const cfg::group_t &,
                                         const cfg::section_t &,
                                         const cfg::position_t &, uintptr_t,
                                         start_addr);
};
} // namespace tep
