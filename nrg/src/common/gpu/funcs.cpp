#include "funcs.hpp"

#include "../../fileline.hpp"

#include <util/concat.hpp>

#include <stdexcept>

namespace {
[[noreturn]] void invalid_readings_type() {
  throw std::logic_error("invalid readings_type value");
}

std::string to_string(nrgprf::readings_type::type rt) {
  switch (rt) {
  case nrgprf::readings_type::power:
    return "power";
  case nrgprf::readings_type::energy:
    return "energy";
  }
  invalid_readings_type();
}
} // namespace

namespace nrgprf {
std::string event_added(unsigned int dev, readings_type::type rt) {
  return fileline(cmmn::concat("added event: device ", std::to_string(dev), " ",
                               to_string(rt), " query"));
}

std::string event_not_supported(unsigned int dev, readings_type::type rt) {
  return fileline(cmmn::concat("device ", std::to_string(dev),
                               " does not support ", to_string(rt),
                               " queries"));
}

std::string event_not_added(unsigned int dev, readings_type::type rt) {
  return fileline(cmmn::concat("device ", std::to_string(dev), " supports ",
                               to_string(rt),
                               " queries, but not adding event due to lack of "
                               "support in previous device(s)"));
}

std::error_code assert_device_count(unsigned int devcount) {
  if (devcount > nrgprf::max_devices)
    return errc::too_many_devices;
  if (!devcount)
    return errc::no_devices_found;
  return {};
}
} // namespace nrgprf
