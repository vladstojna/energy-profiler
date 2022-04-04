#include "../common/gpu/funcs.hpp"
#include "../common/gpu/reader.hpp"
#include "../fileline.hpp"

#include <nrg/sample.hpp>

#include <nonstd/expected.hpp>

#include <nvml.h>

#include <cassert>
#include <iostream>
#include <stdexcept>

namespace {
std::error_code make_error_code(nvmlReturn_t status) {
  return {static_cast<int>(status), nrgprf::gpu_category()};
}

nrgprf::result<unsigned int> get_device_count() {
  using rettype = decltype(get_device_count());
  unsigned int devcount;
  nvmlReturn_t result = nvmlDeviceGetCount(&devcount);
  if (result != NVML_SUCCESS)
    return rettype(nonstd::unexpect, make_error_code(result));
  if (auto ec = nrgprf::assert_device_count(devcount))
    return rettype(nonstd::unexpect, ec);
  return devcount;
}
} // namespace

namespace nrgprf {
lib_handle::lib_handle() {
  nvmlReturn_t result = nvmlInit();
  if (result != NVML_SUCCESS)
    throw exception(::make_error_code(result));
}

lib_handle::~lib_handle() {
  nvmlReturn_t result = nvmlShutdown();
  assert(result == NVML_SUCCESS);
  if (result != NVML_SUCCESS)
    std::cerr << "failed to shutdown NVML: " << nvmlErrorString(result)
              << std::endl;
}

lib_handle::lib_handle(const lib_handle &other) { *this = other; }

lib_handle::lib_handle(lib_handle &&other) : lib_handle(other) {}

lib_handle &lib_handle::operator=(const lib_handle &) {
  nvmlReturn_t result = nvmlInit();
  if (result != NVML_SUCCESS)
    throw exception(::make_error_code(result));
  return *this;
}

lib_handle &lib_handle::operator=(lib_handle &&other) { return *this = other; }

reader_gpu_impl::reader_gpu_impl(readings_type::type rt, device_mask dev_mask,
                                 std::ostream &os)
    : handle(), event_map(), events() {
  if (dev_mask.none())
    throw exception(errc::invalid_device_mask);
  for (auto &val : event_map)
    val.fill(-1);

  auto sup = support(dev_mask);
  if (!sup)
    throw exception(sup.error());
  auto device_cnt = get_device_count();
  if (!device_cnt)
    throw exception(device_cnt.error());
  for (unsigned int i = 0; i < *device_cnt; i++) {
    if (!dev_mask[i])
      continue;

    constexpr size_t sz = NVML_DEVICE_NAME_BUFFER_SIZE;
    char name[sz];
    nvmlDevice_t handle = nullptr;
    if (nvmlReturn_t res;
        (res = nvmlDeviceGetHandleByIndex(i, &handle)) != NVML_SUCCESS)
      throw exception(::make_error_code(res));
    if (nvmlReturn_t res;
        (res = nvmlDeviceGetName(handle, name, sz)) != NVML_SUCCESS)
      throw exception(::make_error_code(res));
    os << fileline("device: ") << i << ", name: " << name << "\n";
    auto sup_dev = support(handle);
    if (!sup_dev)
      throw exception(sup_dev.error());
    for (const auto &elem : type_array) {
      if (!(elem.first & rt))
        continue;
      if (!(*sup_dev & elem.first))
        os << event_not_supported(i, elem.first) << "\n";
      else if (!(*sup & elem.first))
        os << event_not_added(i, elem.first) << "\n";
      else {
        event_map[i][bitpos(elem.first)] = events.size();
        events.push_back({handle, i, elem.second});
        os << event_added(i, elem.first) << "\n";
      }
    }
  }
  if (events.empty())
    throw exception(errc::no_events_added);
}

result<readings_type::type> reader_gpu_impl::support(device_mask devmask) {
  using rettype = result<readings_type::type>;
  if (devmask.none())
    return rettype(nonstd::unexpect, errc::invalid_device_mask);
  lib_handle lib;
  auto devcount = get_device_count();
  if (!devcount)
    return rettype(nonstd::unexpect, devcount.error());
  readings_type::type retval = readings_type::all;
  for (unsigned i = 0; i < *devcount; i++) {
    nvmlDevice_t devhandle = nullptr;
    if (!devmask[i])
      continue;
    if (nvmlReturn_t res;
        (res = nvmlDeviceGetHandleByIndex(i, &devhandle)) != NVML_SUCCESS)
      return rettype(nonstd::unexpect, ::make_error_code(res));
    if (auto sup = support(devhandle))
      retval = retval & *sup;
    else
      return sup;
  }
  if (!retval)
    return rettype(nonstd::unexpect, errc::readings_not_supported);
  return retval;
}

result<readings_type::type>
reader_gpu_impl::support(nvmlDevice_t handle) noexcept {
  assert(handle);
  auto cannot_query_support = [](nvmlReturn_t res) {
    return result<readings_type::type>(nonstd::unexpect,
                                       ::make_error_code(res));
  };

  nvmlReturn_t res;
  readings_type::type rt = readings_type::all;
  unsigned int power;
  if ((res = nvmlDeviceGetPowerUsage(handle, &power)) ==
      NVML_ERROR_NOT_SUPPORTED)
    rt = rt ^ readings_type::power;
  else if (res != NVML_SUCCESS)
    return cannot_query_support(res);
  unsigned long long energy;
  if ((res = nvmlDeviceGetTotalEnergyConsumption(handle, &energy)) ==
      NVML_ERROR_NOT_SUPPORTED)
    rt = rt ^ readings_type::energy;
  else if (res != NVML_SUCCESS)
    return cannot_query_support(res);
  return rt;
}

bool reader_gpu_impl::read_energy(sample &s, size_t stride, nvmlDevice_t handle,
                                  std::error_code &ec) noexcept {
  unsigned long long energy;
  nvmlReturn_t result = nvmlDeviceGetTotalEnergyConsumption(handle, &energy);
  if (result != NVML_SUCCESS) {
    ec = ::make_error_code(result);
    return false;
  }
  s.data.gpu_energy[stride] = energy;
  ec.clear();
  return true;
}

bool reader_gpu_impl::read_power(sample &s, size_t stride, nvmlDevice_t handle,
                                 std::error_code &ec) noexcept {
  unsigned int power;
  nvmlReturn_t result = nvmlDeviceGetPowerUsage(handle, &power);
  if (result != NVML_SUCCESS) {
    ec = ::make_error_code(result);
    return false;
  }
  s.data.gpu_power[stride] = power;
  ec.clear();
  return true;
}

result<units_power>
reader_gpu_impl::get_board_power(const sample &s, uint8_t dev) const noexcept {
  return get_value<readings_type::power, milliwatts<uint32_t>, units_power>(
      s.data.gpu_power, dev);
}

result<units_energy>
reader_gpu_impl::get_board_energy(const sample &s, uint8_t dev) const noexcept {
  return get_value<readings_type::energy, millijoules<uint32_t>, units_energy>(
      s.data.gpu_energy, dev);
}
} // namespace nrgprf

#include "../common/gpu/reader.inl"
