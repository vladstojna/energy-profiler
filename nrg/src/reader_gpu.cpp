// reader_gpu.cpp

#include "reader_gpu.hpp"

#include <algorithm>
#include <cstdio>
#include <cassert>

#if defined(GPU_NV)
#include <nvml.h>
#elif defined(GPU_AMD)
#include <rocm_smi.h>
#endif

using namespace nrgprf;


std::string error_str(const char* prefix, nvmlReturn_t result)
{
    return std::string(prefix)
        .append(": ")
        .append(nvmlErrorString(result));
}


// nvml_handle


detail::nvml_handle::nvml_handle(error& ec)
{
    nvmlReturn_t result = nvmlInit();
    if (result != NVML_SUCCESS)
        ec = { error_code::READER_GPU, error_str("Failed to initialise NVML", result) };
}

detail::nvml_handle::~nvml_handle() noexcept
{
    nvmlReturn_t result = nvmlShutdown();
    if (result != NVML_SUCCESS)
        fprintf(stderr, "failed to shutdown NVML: %s\n", nvmlErrorString(result));
}

detail::nvml_handle::nvml_handle(const nvml_handle& other) noexcept
{
    *this = other;
}

detail::nvml_handle::nvml_handle(nvml_handle&& other) noexcept :
    nvml_handle(other)
{}

detail::nvml_handle& detail::nvml_handle::operator=(const nvml_handle& other) noexcept
{
    (void)other;
    nvmlReturn_t result = nvmlInit();
    if (result != NVML_SUCCESS)
        fprintf(stderr, "failed to initialise NVML: %s\n", nvmlErrorString(result));
    return *this;
}

detail::nvml_handle& detail::nvml_handle::operator=(nvml_handle&& other) noexcept
{
    return *this = other;
}


// nvml_handle


reader_gpu::reader_gpu(uint8_t dev_mask, size_t offset, error& ec) :
    _nvml_handle(detail::nvml_handle(ec)),
    _offset(offset),
    _event_map{ -1 },
    _active_handles()
{
    nvmlReturn_t result;
    unsigned int device_cnt;

    // error constructing nvml_handle
    if (ec)
        return;

    result = nvmlDeviceGetCount(&device_cnt);
    if (result != NVML_SUCCESS)
    {
        ec = { error_code::READER_GPU, error_str("Failed to obtain device count", result) };
        return;
    }
    if (device_cnt > MAX_SOCKETS)
    {
        ec = { error_code::TOO_MANY_DEVICES, "Too many devices (a maximum of 8 is supported)" };
        return;
    }
    printf("Found %u device%s\n", device_cnt, device_cnt != 1 ? "s" : "");
    for (unsigned int i = 0; i < device_cnt; i++)
    {
        nvmlDevice_t handle;
        char name[NVML_DEVICE_NAME_BUFFER_SIZE];

        if (!(dev_mask & (1 << i)))
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
        printf("Device name: %s\n", name);
        _event_map[i] = _active_handles.size();
        _active_handles.push_back(handle);
    }
    assert(_active_handles.size() == device_cnt);
}

reader_gpu::reader_gpu(uint8_t dev_mask, const reader_rapl& reader, error& ec) :
    reader_gpu(dev_mask, reader.num_events(), ec)
{}

reader_gpu::reader_gpu(uint8_t dev_mask, error& ec) :
    reader_gpu(dev_mask, 0, ec)
{}

reader_gpu::reader_gpu(const reader_rapl& reader, error& ec) :
    reader_gpu(0xff, reader.num_events(), ec)
{}

reader_gpu::reader_gpu(error& ec) :
    reader_gpu(0xff, 0, ec)
{}


error reader_gpu::read(sample& s) const
{
    for (size_t idx = 0; idx < _active_handles.size(); idx++)
    {
        error err = read(s, idx);
        if (err)
            return err;
    }
    return error::success();
}


error reader_gpu::read(sample& s, int8_t ev_idx) const
{
    unsigned int power;
    nvmlReturn_t result = nvmlDeviceGetPowerUsage(_active_handles[ev_idx], &power);
    if (result != NVML_SUCCESS)
        return { error_code::READER_GPU, nvmlErrorString(result) };
    s.set(_offset + ev_idx, power);
    return error::success();
}


int8_t reader_gpu::event_idx(uint8_t device) const
{
    return _event_map[device];
}

size_t reader_gpu::num_events() const
{
    return _active_handles.size();
}

result<uint64_t> reader_gpu::get_board_power(const sample& s, uint8_t dev) const
{
    int8_t idx = event_idx(dev);
    if (idx < 0)
        return error(error_code::NO_EVENT);
    return s.get(_offset + idx);
}
