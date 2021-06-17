// reader_gpu.cpp

#include <nrg/reader_gpu.hpp>
#include <nrg/sample.hpp>
#include <util/concat.hpp>

#include <algorithm>
#include <iostream>
#include <cassert>
#include <stdexcept>

#if defined(GPU_NONE)
#undef GPU_NONE
#endif

#if !defined(GPU_NV) && !defined(GPU_AMD)
#define GPU_NONE
#endif

#if defined(GPU_NV)
#include <nvml.h>
#elif defined(GPU_AMD)
#include <rocm_smi/rocm_smi.h>
#endif

#include "util.hpp"

using namespace nrgprf;


#if defined(GPU_NV)

std::string error_str(const char* prefix, nvmlReturn_t result)
{
    return std::string(prefix)
        .append(": ")
        .append(nvmlErrorString(result));
}

#elif defined(GPU_AMD)

std::string error_str(const char* prefix, rsmi_status_t result)
{
    const char* str;
    rsmi_status_string(result, &str);
    return std::string(prefix)
        .append(": ")
        .append(str);
}

#endif


// begin lib_handle

#if !defined(GPU_NONE)

struct lib_handle
{
    lib_handle(error& ec);
    ~lib_handle();

    lib_handle(const lib_handle& other);
    lib_handle(lib_handle&& other);

    lib_handle& operator=(const lib_handle& other);
    lib_handle& operator=(lib_handle&& other);
};

#endif // !defined(GPU_NONE)

#if defined(GPU_NV)

lib_handle::lib_handle(error& ec)
{
    nvmlReturn_t result = nvmlInit();
    if (result != NVML_SUCCESS)
        ec = { error_code::READER_GPU, error_str("failed to initialise NVML", result) };
}

lib_handle::~lib_handle()
{
    nvmlReturn_t result = nvmlShutdown();
    assert(result == NVML_SUCCESS);
    if (result != NVML_SUCCESS)
        std::cerr << error_str("failed to shutdown NVML", result) << std::endl;
}

lib_handle::lib_handle(const lib_handle& other)
{
    *this = other;
}

lib_handle::lib_handle(lib_handle&& other) :
    lib_handle(other)
{}

lib_handle& lib_handle::operator=(const lib_handle& other)
{
    (void)other;
    nvmlReturn_t result = nvmlInit();
    if (result != NVML_SUCCESS)
        throw std::runtime_error(error_str("failed to initialise NVML", result));
    return *this;
}

lib_handle& lib_handle::operator=(lib_handle&& other)
{
    return *this = other;
}

#elif defined(GPU_AMD)

lib_handle::lib_handle(error& ec)
{
    rsmi_status_t result = rsmi_init(0);
    if (result != RSMI_STATUS_SUCCESS)
        ec = { error_code::READER_GPU, error_str("failed to initialise ROCm SMI", result) };
}

lib_handle::~lib_handle()
{
    rsmi_status_t result = rsmi_shut_down();
    assert(result == RSMI_STATUS_SUCCESS);
    if (result != RSMI_STATUS_SUCCESS)
        std::cerr << error_str("failed to shutdown ROCm SMI", result) << std::endl;
}

lib_handle::lib_handle(const lib_handle& other)
{
    *this = other;
}

lib_handle::lib_handle(lib_handle&& other) :
    lib_handle(other)
{}

lib_handle& lib_handle::operator=(const lib_handle& other)
{
    (void)other;
    rsmi_status_t result = rsmi_init(0);
    if (result != RSMI_STATUS_SUCCESS)
        throw std::runtime_error(error_str("failed to initialise ROCm SMI", result));
    return *this;
}

lib_handle& lib_handle::operator=(lib_handle&& other)
{
    return *this = other;
}

#endif

// end lib_handle


// begin impl

struct reader_gpu::impl
{
#if !defined(GPU_NONE)
    int8_t event_map[max_devices];
    lib_handle handle;
#endif // !defined(GPU_NONE)
#if defined(GPU_NV)
    std::vector<nvmlDevice_t> active_handles;
#elif defined(GPU_AMD)
    std::vector<uint32_t> active_handles;
#endif

    impl(device_mask dmask, error& ec);

    error read(sample& s) const;
    error read(sample& s, uint8_t ev_idx) const;

    int8_t event_idx(uint8_t device) const;
    size_t num_events() const;

    result<units_power> get_board_power(const sample& s, uint8_t dev) const;
};

// constructor

#if defined(GPU_NV)

reader_gpu::impl::impl(device_mask dev_mask, error& ec) :
    event_map(),
    handle(ec),
    active_handles()
{
    // error constructing lib_handle
    if (ec)
        return;

    for (size_t ix = 0; ix < max_devices; ix++)
        event_map[ix] = -1;

    unsigned int device_cnt;
    nvmlReturn_t result = nvmlDeviceGetCount(&device_cnt);
    if (result != NVML_SUCCESS)
    {
        ec = { error_code::READER_GPU, error_str("Failed to obtain device count", result) };
        return;
    }
    if (device_cnt > max_devices)
    {
        ec = { error_code::TOO_MANY_DEVICES, "Too many devices (a maximum of 8 is supported)" };
        return;
    }
    for (unsigned int i = 0; i < device_cnt; i++)
    {
        nvmlDevice_t handle;
        char name[NVML_DEVICE_NAME_BUFFER_SIZE];

        if (!dev_mask[i])
            continue;

        result = nvmlDeviceGetHandleByIndex(i, &handle);
        if (result != NVML_SUCCESS)
        {
            ec = { error_code::READER_GPU, error_str("Failed to get device handle", result) };
            return;
        }
        result = nvmlDeviceGetName(handle, name, NVML_DEVICE_NAME_BUFFER_SIZE);
        if (result != NVML_SUCCESS)
        {
            ec = { error_code::READER_GPU, error_str("Failed to get device name", result) };
            return;
        }

        std::cout << fileline(cmmn::concat("device: ", std::to_string(i), ", name: ", name, "\n"));
        event_map[i] = active_handles.size();
        active_handles.push_back(handle);
    }
    assert(active_handles.size() == device_cnt);
}

#elif defined(GPU_AMD)

reader_gpu::impl::impl(device_mask dev_mask, error& ec) :
    event_map(),
    handle(ec),
    active_handles()
{
    // error constructing lib_handle
    if (ec)
        return;

    for (size_t ix = 0; ix < max_devices; ix++)
        event_map[ix] = -1;

    rsmi_version_t version;
    rsmi_status_t result = rsmi_version_get(&version);
    if (result != RSMI_STATUS_SUCCESS)
    {
        ec = { error_code::READER_GPU, error_str("Failed to obtain lib build version", result) };
        return;
    }

    std::cout << fileline("ROCm SMI version info: ");
    std::cout << "major: " << version.major << ", minor: " << version.minor
        << ", patch: " << version.patch << ", build: " << version.build << "\n";

    uint32_t device_cnt;
    result = rsmi_num_monitor_devices(&device_cnt);
    if (result != RSMI_STATUS_SUCCESS)
    {
        ec = { error_code::READER_GPU, error_str("Failed to obtain device count", result) };
        return;
    }
    if (device_cnt > max_devices)
    {
        ec = { error_code::TOO_MANY_DEVICES, "Too many devices (a maximum of 8 is supported)" };
        return;
    }
    for (uint32_t dev_idx = 0; dev_idx < device_cnt; dev_idx++)
    {
        char name[512];
        uint64_t dev_pci_id;

        if (!dev_mask[dev_idx])
            continue;

        result = rsmi_dev_pci_id_get(dev_idx, &dev_pci_id);
        if (result != RSMI_STATUS_SUCCESS)
        {
            ec = { error_code::READER_GPU, error_str("Failed to get device PCI ID", result) };
            return;
        }
        result = rsmi_dev_name_get(dev_idx, name, sizeof(name));
        if (result != RSMI_STATUS_SUCCESS && result != RSMI_STATUS_INSUFFICIENT_SIZE)
        {
            ec = { error_code::READER_GPU, error_str("Failed to get device name", result) };
            return;
        }
        std::cout << fileline("");
        std::cout << "idx: " << dev_idx
            << ", PCI id: " << dev_pci_id
            << ", name: " << name << "\n";
        event_map[dev_idx] = active_handles.size();
        active_handles.push_back(dev_idx);
    }
    assert(active_handles.size() == device_cnt);
}

#else

reader_gpu::impl::impl(device_mask dev_mask, error& ec)
{
    (void)dev_mask;
    (void)ec;
    std::cout << fileline("No-op GPU reader\n");
}

#endif

// read all

#if !defined(GPU_NONE)

error reader_gpu::impl::read(sample& s) const
{
    for (size_t idx = 0; idx < active_handles.size(); idx++)
    {
        error err = read(s, idx);
        if (err)
            return err;
    }
    return error::success();
}

#else

error reader_gpu::impl::read(sample& s) const
{
    (void)s;
    return error::success();
}

#endif

// read single

#if defined(GPU_NV)

error reader_gpu::impl::read(sample& s, uint8_t ev_idx) const
{
    unsigned int power;
    nvmlReturn_t result = nvmlDeviceGetPowerUsage(active_handles[ev_idx], &power);
    if (result != NVML_SUCCESS)
        return { error_code::READER_GPU, nvmlErrorString(result) };
    // NVML returns milliwatts, multiply by 1000 to get microwatts
    s.at_gpu(ev_idx) = power * 1000;
    return error::success();
}

#elif defined(GPU_AMD)

error reader_gpu::impl::read(sample& s, uint8_t ev_idx) const
{
    uint64_t power;
    rsmi_status_t result = rsmi_dev_power_ave_get(active_handles[ev_idx], 0, &power);
    if (result != RSMI_STATUS_SUCCESS)
    {
        const char* str;
        rsmi_status_string(result, &str);
        return { error_code::READER_GPU, str };
    }
    // ROCm SMI reads power in microwatts
    s.at_gpu(ev_idx) = power;
    return error::success();
}

#else

error reader_gpu::impl::read(sample& s, uint8_t ev_idx) const
{
    (void)s;
    (void)ev_idx;
    return error::success();
}

#endif

// queries

#if !defined(GPU_NONE)

int8_t reader_gpu::impl::event_idx(uint8_t device) const
{
    return event_map[device];
}

size_t reader_gpu::impl::num_events() const
{
    return active_handles.size();
}

#else

int8_t reader_gpu::impl::event_idx(uint8_t device) const
{
    (void)device;
    return -1;
}

size_t reader_gpu::impl::num_events() const
{
    return 0;
}

#endif

#if !defined(GPU_NONE)

result<units_power> reader_gpu::impl::get_board_power(const sample& s, uint8_t dev) const
{
    int8_t idx = event_idx(dev);
    if (idx < 0)
        return error(error_code::NO_EVENT);
    result<sample::value_type> result = s.at_gpu(idx);
    if (!result)
        return std::move(result.error());
    return result.value();
}

#else

result<units_power> reader_gpu::impl::get_board_power(const sample& s, uint8_t dev) const
{
    (void)s;
    (void)dev;
    return error(error_code::NO_EVENT);
}

#endif


// end impl


reader_gpu::reader_gpu(device_mask dev_mask, error& ec) :
    _impl(std::make_unique<reader_gpu::impl>(dev_mask, ec))
{}

reader_gpu::reader_gpu(error& ec) :
    reader_gpu(device_mask(0xff), ec)
{}

reader_gpu::reader_gpu(const reader_gpu& other) :
    _impl(std::make_unique<reader_gpu::impl>(*other.pimpl()))
{}

reader_gpu& reader_gpu::operator=(const reader_gpu& other)
{
    _impl = std::make_unique<reader_gpu::impl>(*other.pimpl());
    return *this;
}

reader_gpu::reader_gpu(reader_gpu&& other) = default;
reader_gpu& reader_gpu::operator=(reader_gpu && other) = default;
reader_gpu::~reader_gpu() = default;

error reader_gpu::read(sample & s) const
{
    return pimpl()->read(s);
}

error reader_gpu::read(sample & s, uint8_t ev_idx) const
{
    return pimpl()->read(s, ev_idx);
}

int8_t reader_gpu::event_idx(uint8_t device) const
{
    return pimpl()->event_idx(device);
}

size_t reader_gpu::num_events() const
{
    return pimpl()->num_events();
}

result<units_power> reader_gpu::get_board_power(const sample & s, uint8_t dev) const
{
    return pimpl()->get_board_power(s, dev);
}

std::vector<reader_gpu::dev_pwr> reader_gpu::get_board_power(const sample & s) const
{
    std::vector<reader_gpu::dev_pwr> retval;
    for (uint32_t d = 0; d < max_devices; d++)
    {
        auto pwr = get_board_power(s, d);
        if (pwr)
            retval.push_back({ d, std::move(pwr.value()) });
    }
    return retval;
}

const reader_gpu::impl* reader_gpu::pimpl() const
{
    assert(_impl);
    return _impl.get();
}

reader_gpu::impl* reader_gpu::pimpl()
{
    assert(_impl);
    return _impl.get();
}
