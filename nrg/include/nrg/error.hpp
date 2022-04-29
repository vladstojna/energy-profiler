// error.hpp

#pragma once

#include <memory>
#include <string>

#include <system_error>

namespace nrgprf {
enum class errc : uint32_t;
enum class error_cause : uint32_t;
} // namespace nrgprf

namespace std {
template <> struct is_error_code_enum<nrgprf::errc> : std::true_type {};
template <>
struct is_error_condition_enum<nrgprf::error_cause> : std::true_type {};
} // namespace std

namespace nrgprf {
enum class errc : uint32_t {
  not_implemented = 1,
  no_events_added,
  no_such_event,
  no_sockets_found,
  no_devices_found,
  too_many_sockets,
  too_many_devices,
  invalid_domain_name,
  file_format_error,
  file_format_version_error,
  operation_not_supported,
  energy_readings_not_supported,
  power_readings_not_supported,
  readings_not_supported,
  readings_not_valid,
  package_num_error,
  package_num_wrong_domain,
  invalid_socket_mask,
  invalid_device_mask,
  invalid_location_mask,
  unsupported_units,
  unknown_error,
};

enum class error_cause : uint32_t {
  gpu_lib_error = 1,
  setup_error,
  query_error,
  read_error,
  system_error,
  invalid_argument,
  readings_support_error,
  other,
  unknown,
};

struct exception : std::system_error {
  using system_error::system_error;
};

std::error_code make_error_code(errc) noexcept;
std::error_condition make_error_condition(error_cause) noexcept;

const std::error_category &generic_category() noexcept;
const std::error_category &gpu_category() noexcept;
} // namespace nrgprf
