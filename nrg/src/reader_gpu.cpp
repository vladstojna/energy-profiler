// reader_gpu.cpp

#include "reader_gpu.hpp"

#include <algorithm>
#include <cstdio>

#if defined(GPU_NV)
extern "C"
{
#include <nvml.h>
}
#elif defined(GPU_AMD)
#include <rocm_smi.h>
#endif

using namespace nrgprf;

// begin helper functions

bool dev_is_set(uint8_t dev_mask, uint8_t skt)
{
    return dev_mask & (1 << skt);
}

// end helper functions

reader_gpu::reader_gpu(uint8_t dev_mask, error& ec) :
    _dev_mask(dev_mask),
    _events(),
    _handles()
{
    nvmlReturn_t result;
    unsigned int device_cnt;

    std::fill(std::begin(_handles), std::end(_handles), nullptr);

    result = nvmlInit();
    if (result != NVML_SUCCESS)
    {
        ec = { error_code::SETUP_ERROR, nvmlErrorString(result) };
        return;
    }
    result = nvmlDeviceGetCount(&device_cnt);
    if (result != NVML_SUCCESS)
    {
        ec = { error_code::SETUP_ERROR, nvmlErrorString(result) };
        return;
    }
    if (device_cnt > MAX_SOCKETS)
    {
        ec = { error_code::SETUP_ERROR, "Too many devices (a maximum of 8 is supported)" };
        return;
    }
    printf("Found %u device%s\n", device_cnt, device_cnt != 1 ? "s" : "");
    for (unsigned int i = 0; i < device_cnt; i++)
    {
        char name[NVML_DEVICE_NAME_BUFFER_SIZE];
        if (!dev_is_set(dev_mask, i))
            continue;
        result = nvmlDeviceGetHandleByIndex(i, &_handles[i]);
        if (result != NVML_SUCCESS)
        {
            ec = { error_code::SETUP_ERROR, nvmlErrorString(result) };
            return;
        }
        result = nvmlDeviceGetName(_handles[i], name, NVML_DEVICE_NAME_BUFFER_SIZE);
        if (result != NVML_SUCCESS)
        {
            ec = { error_code::SETUP_ERROR, nvmlErrorString(result) };
            return;
        }
        _events[i] = { static_cast<int8_t>(i) };
        printf("Device name: %s\n", name);
    }
}

reader_gpu::~reader_gpu() noexcept
{
    nvmlReturn_t result = nvmlShutdown();
    if (result != NVML_SUCCESS)
        fprintf(stderr, "failed to shutdown NVML: %s\n", nvmlErrorString(result));
}

const event_gpu& reader_gpu::event(size_t dev) const
{
    return _events[dev];
}

error reader_gpu::read(sample& s) const
{
    nvmlReturn_t result;
    for (auto i = 0; i < MAX_SOCKETS; i++)
    {
        if (_handles[i] != nullptr)
        {
            // nvml only writes a single unsigned int,
            // since power is a scalar value greater than zero
            // however, our samples store 64-bit signed integers, but these can fit any 32-bit data
            // thus making this cast safe
            result = nvmlDeviceGetPowerUsage(
                _handles[i],
                reinterpret_cast<unsigned int*>(&s.values()[i]));
            if (result != NVML_SUCCESS)
                return { error_code::READ_ERROR, nvmlErrorString(result) };
        }
    }
    return error::success();
}
