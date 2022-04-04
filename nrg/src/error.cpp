// error.cpp
#include "gpu_category.hpp"

#include <nrg/error.hpp>

#include <cassert>
#include <iostream>

namespace {
struct generic_category_t : std::error_category {
  const char *name() const noexcept override;
  std::string message(int) const override;
  std::error_condition default_error_condition(int) const noexcept override;
};

struct error_cause_category_t : std::error_category {
  const char *name() const noexcept override;
  std::string message(int) const override;
  bool equivalent(const std::error_code &, int) const noexcept override;
};

const generic_category_t generic_category_v;
const error_cause_category_t error_cause_category_v;
const nrgprf::gpu_category_t gpu_category_v;

const char *generic_category_t::name() const noexcept { return "nrg-lib"; }

std::string generic_category_t::message(int ev) const {
  using nrgprf::errc;
  switch (static_cast<errc>(ev)) {
  case errc::not_implemented:
    return "feature not implemented";
  case errc::no_events_added:
    return "no events were added";
  case errc::no_such_event:
    return "no such event exists";
  case errc::no_sockets_found:
    return "no CPU sockets were found";
  case errc::no_devices_found:
    return "no GPU devices were found";
  case errc::too_many_sockets:
    return "more CPU sockets found than maximum supported";
  case errc::too_many_devices:
    return "more GPU devices found than maximum supported";
  case errc::invalid_domain_name:
    return "invalid RAPL domain name";
  case errc::file_format_error:
    return "invalid CPU counters file format";
  case errc::file_format_version_error:
    return "invalid format version in CPU counters file";
  case errc::operation_not_supported:
    return "operation not supported";
  case errc::energy_readings_not_supported:
    return "GPU does not support energy readings";
  case errc::power_readings_not_supported:
    return "GPU does not support power readings";
  case errc::readings_not_supported:
    return "GPU does not support energy or power readings";
  case errc::readings_not_valid:
    return "counter readings are not valid";
  case errc::package_num_error:
    return "error reading package number from RAPL powercap package domain";
  case errc::package_num_wrong_domain:
    return "attempt to read the package number from a non-package RAPL domain";
  case errc::invalid_socket_mask:
    return "invalid CPU socket mask (no sockets set)";
  case errc::invalid_device_mask:
    return "invalid GPU device mask (no devices set)";
  case errc::invalid_location_mask:
    return "invalid sensor location mask (no sensors set)";
  case errc::unsupported_units:
    return "unsupported units";
  case errc::unknown_error:
    return "unknown error";
  }
  return "(unrecognized nrg error code)";
}

std::error_condition
generic_category_t::default_error_condition(int ev) const noexcept {
  using nrgprf::errc;
  using nrgprf::error_cause;
  switch (static_cast<errc>(ev)) {
  case errc::no_events_added:
  case errc::no_sockets_found:
  case errc::no_devices_found:
  case errc::too_many_sockets:
  case errc::too_many_devices:
  case errc::invalid_domain_name:
  case errc::file_format_error:
  case errc::file_format_version_error:
  case errc::package_num_error:
  case errc::package_num_wrong_domain:
    return error_cause::setup_error;
  case errc::energy_readings_not_supported:
  case errc::power_readings_not_supported:
  case errc::readings_not_supported:
    return error_cause::readings_support_error;
  case errc::not_implemented:
  case errc::operation_not_supported:
    return error_cause::other;
  case errc::unknown_error:
    return error_cause::unknown;
  case errc::no_such_event:
  case errc::unsupported_units:
    return error_cause::query_error;
  case errc::readings_not_valid:
    return error_cause::read_error;
  case errc::invalid_socket_mask:
  case errc::invalid_device_mask:
  case errc::invalid_location_mask:
    return error_cause::invalid_argument;
  }
  return error_cause::unknown;
}

const char *error_cause_category_t::name() const noexcept {
  return "error-cause";
}

std::string error_cause_category_t::message(int ev) const {
  using nrgprf::error_cause;
  switch (static_cast<error_cause>(ev)) {
  case error_cause::gpu_lib_error:
    return "GPU library error";
  case error_cause::setup_error:
    return "error during reader setup";
  case error_cause::query_error:
    return "error querying value";
  case error_cause::read_error:
    return "error reading counters";
  case error_cause::system_error:
    return "system error";
  case error_cause::invalid_argument:
    return "invalid argument";
  case error_cause::readings_support_error:
    return "error querying GPU energy/power support";
  case error_cause::other:
    return "other error";
  case error_cause::unknown:
    return "unknown error cause";
  }
  return "(unrecognized error condition)";
}

bool error_cause_category_t::equivalent(const std::error_code &ec,
                                        int cv) const noexcept {
  using nrgprf::error_cause;
  auto cond = static_cast<error_cause>(cv);
  if (ec.category() == std::system_category())
    return cond == error_cause::system_error;
  if (ec.category() == nrgprf::gpu_category())
    return cond == error_cause::gpu_lib_error;
  if (ec.category() == nrgprf::generic_category())
    return cond == ec.category().default_error_condition(ec.value());
  return false;
}
} // namespace

namespace nrgprf {
std::error_code make_error_code(errc x) noexcept {
  return std::error_code{static_cast<int>(x), generic_category()};
}

std::error_condition make_error_condition(error_cause x) noexcept {
  return std::error_condition{static_cast<int>(x), error_cause_category_v};
}

const std::error_category &generic_category() noexcept {
  return generic_category_v;
}

const std::error_category &gpu_category() noexcept { return gpu_category_v; }
} // namespace nrgprf
