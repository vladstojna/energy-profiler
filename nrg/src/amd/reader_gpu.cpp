#include "../common/gpu/funcs.hpp"
#include "../common/gpu/reader.hpp"
#include "../fileline.hpp"

#include <nrg/sample.hpp>

#include <nonstd/expected.hpp>

#include <rocm_smi/rocm_smi.h>

#include <cassert>
#include <iostream>
#include <stdexcept>

namespace {
std::error_code make_error_code(rsmi_status_t status) {
  return {static_cast<int>(status), nrgprf::gpu_category()};
}

nrgprf::result<unsigned int> get_device_count() {
  using rettype = decltype(get_device_count());
  uint32_t devcount;
  rsmi_status_t result = rsmi_num_monitor_devices(&devcount);
  if (result != RSMI_STATUS_SUCCESS)
    return rettype(nonstd::unexpect, make_error_code(result));
  if (auto ec = nrgprf::assert_device_count(devcount))
    return rettype(nonstd::unexpect, ec);
  return devcount;
}
} // namespace

namespace nrgprf {
lib_handle::lib_handle() {
  rsmi_status_t result = rsmi_init(0);
  if (result != RSMI_STATUS_SUCCESS)
    throw exception(::make_error_code(result));
}

lib_handle::~lib_handle() {
  rsmi_status_t result = rsmi_shut_down();
  assert(result == RSMI_STATUS_SUCCESS);
  if (result != RSMI_STATUS_SUCCESS) {
    const char *str = nullptr;
    if (RSMI_STATUS_SUCCESS != rsmi_status_string(result, &str))
      str = "(unknown status code)";
    std::cerr << "failed to shutdown ROCm SMI: " << str << std::endl;
  }
}

lib_handle::lib_handle(const lib_handle &other) { *this = other; }

lib_handle::lib_handle(lib_handle &&other) : lib_handle(other) {}

lib_handle &lib_handle::operator=(const lib_handle &) {
  rsmi_status_t result = rsmi_init(0);
  if (result != RSMI_STATUS_SUCCESS)
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

  rsmi_version_t version;
  if (rsmi_status_t res;
      (res = rsmi_version_get(&version)) != RSMI_STATUS_SUCCESS)
    throw exception(::make_error_code(res));

  os << fileline("ROCm SMI version info: ");
  os << "major: " << version.major << ", minor: " << version.minor
     << ", patch: " << version.patch << ", build: " << version.build << "\n";

  auto sup = support(dev_mask);
  if (!sup)
    throw exception(sup.error());
  auto device_cnt = get_device_count();
  if (!device_cnt)
    throw exception(sup.error());
  for (uint32_t dev_idx = 0; dev_idx < *device_cnt; dev_idx++) {
    if (!dev_mask[dev_idx])
      continue;

    char name[512];
    uint64_t pciid;
    if (rsmi_status_t res = rsmi_dev_pci_id_get(dev_idx, &pciid);
        res != RSMI_STATUS_SUCCESS)
      throw exception(::make_error_code(res));
    if (rsmi_status_t res = rsmi_dev_name_get(dev_idx, name, sizeof(name));
        res != RSMI_STATUS_SUCCESS && res != RSMI_STATUS_INSUFFICIENT_SIZE)
      throw exception(::make_error_code(res));
    os << fileline("") << "idx: " << dev_idx << ", PCI id: " << pciid
       << ", name: " << name << "\n";

    auto sup_dev = support(dev_idx);
    if (!sup_dev)
      throw exception(sup_dev.error());
    for (const auto &elem : type_array) {
      if (!(elem.first & rt))
        continue;
      if (!(*sup_dev & elem.first))
        os << event_not_supported(dev_idx, elem.first) << "\n";
      else if (!(*sup & elem.first))
        os << event_not_added(dev_idx, elem.first) << "\n";
      else {
        event_map[dev_idx][bitpos(elem.first)] = events.size();
        events.push_back({dev_idx, dev_idx, elem.second});
        os << event_added(dev_idx, elem.first) << "\n";
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
    return rettype(nonstd::unexpect, std::move(devcount).error());
  ;
  readings_type::type retval = readings_type::all;
  for (unsigned i = 0; i < *devcount; i++) {
    if (!devmask[i])
      continue;
    if (auto sup = support(i))
      retval = retval & *sup;
    else
      return sup;
  }
  if (!retval)
    return rettype(nonstd::unexpect, errc::readings_not_supported);
  return retval;
}

result<readings_type::type> reader_gpu_impl::support(gpu_handle h) noexcept {
  using rettype = result<readings_type::type>;
  rsmi_status_t res;
  readings_type::type rt = readings_type::all;
  if (uint64_t power;
      (res = rsmi_dev_power_ave_get(h, 0, &power)) == RSMI_STATUS_NOT_SUPPORTED)
    rt = rt ^ readings_type::power;
  else if (res != RSMI_STATUS_SUCCESS)
    return rettype(nonstd::unexpect, ::make_error_code(res));
  return rt ^ readings_type::energy;
}

bool reader_gpu_impl::read_energy(sample &, size_t, gpu_handle,
                                  std::error_code &ec) noexcept {
  ec = errc::energy_readings_not_supported;
  return false;
}

bool reader_gpu_impl::read_power(sample &s, size_t stride, gpu_handle handle,
                                 std::error_code &ec) noexcept {
  uint64_t power;
  rsmi_status_t result = rsmi_dev_power_ave_get(handle, 0, &power);
  if (result != RSMI_STATUS_SUCCESS) {
    ec = ::make_error_code(result);
    return false;
  }
  s.data.gpu_power[stride] = power;
  ec.clear();
  return true;
}

result<units_power>
reader_gpu_impl::get_board_power(const sample &s, uint8_t dev) const noexcept {
  return get_value<readings_type::power, microwatts<uint32_t>, units_power>(
      s.data.gpu_power, dev);
}

result<units_energy> reader_gpu_impl::get_board_energy(const sample &,
                                                       uint8_t) const noexcept {
  return result<units_energy>(nonstd::unexpect, errc::no_such_event);
}
} // namespace nrgprf

#include "../common/gpu/reader.inl"
