// reader_gpu.cpp

#include "reader_gpu.hpp"

#include <algorithm>
#include <iostream>
#include <cassert>
#include <stdexcept>

#if defined(GPU_NV)
#include <nvml.h>
#elif defined(GPU_AMD)
// TODO
#endif

using namespace nrgprf;


#if defined(GPU_NV)

std::string error_str(const char* prefix, nvmlReturn_t result)
{
    return std::string(prefix)
        .append(": ")
        .append(nvmlErrorString(result));
}

#endif


// lib_handle

#if defined(GPU_NV)

struct lib_handle
{
    lib_handle(error& ec);
    ~lib_handle();

    lib_handle(const lib_handle& other);
    lib_handle(lib_handle&& other);

    lib_handle& operator=(const lib_handle& other);
    lib_handle& operator=(lib_handle&& other);
};

// lib_handle


lib_handle::lib_handle(error& ec)
{
    nvmlReturn_t result = nvmlInit();
    if (result != NVML_SUCCESS)
        ec = { error_code::READER_GPU, error_str("failed to initialise NVML", result) };
}

lib_handle::~lib_handle()
{
    nvmlReturn_t result = nvmlShutdown();
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

#endif

// lib_handle


// begin impl

struct reader_gpu::impl
{
#if defined(GPU_NV)
    size_t offset;
    int8_t event_map[MAX_SOCKETS];
    lib_handle handle;
    std::vector<nvmlDevice_t> active_handles;
#endif

    impl(uint8_t dmask, size_t os, error& ec);

    error read(sample& s) const;
    error read(sample& s, int8_t ev_idx) const;

    int8_t event_idx(uint8_t device) const;
    size_t num_events() const;

    result<units_power> get_board_power(const sample& s, uint8_t dev) const;
};


#if defined(GPU_NV)

reader_gpu::impl::impl(uint8_t dev_mask, size_t os, error& ec) :
    offset(os),
    event_map(),
    handle(ec),
    active_handles()
{
    // error constructing lib_handle
    if (ec)
        return;

    for (size_t ix = 0; ix < MAX_SOCKETS; ix++)
        event_map[ix] = -1;

    unsigned int device_cnt;
    nvmlReturn_t result = nvmlDeviceGetCount(&device_cnt);
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
        std::cout << "device: " << i << ", name: " << name << "\n";
        event_map[i] = active_handles.size();
        active_handles.push_back(handle);
    }
    assert(active_handles.size() == device_cnt);
}

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

error reader_gpu::impl::read(sample& s, int8_t ev_idx) const
{
    unsigned int power;
    nvmlReturn_t result = nvmlDeviceGetPowerUsage(active_handles[ev_idx], &power);
    if (result != NVML_SUCCESS)
        return { error_code::READER_GPU, nvmlErrorString(result) };
    s.set(offset + ev_idx, power);
    return error::success();
}

int8_t reader_gpu::impl::event_idx(uint8_t device) const
{
    return event_map[device];
}

size_t reader_gpu::impl::num_events() const
{
    return active_handles.size();
}

result<units_power> reader_gpu::impl::get_board_power(const sample& s, uint8_t dev) const
{
    int8_t idx = event_idx(dev);
    if (idx < 0)
        return error(error_code::NO_EVENT);
    // NVML returns milliwatts, multiply by 1000 to get microwatts
    return s.get(offset + idx) * 1000;
}

#else

reader_gpu::impl::impl(uint8_t dev_mask, size_t os, error& ec)
{
    (void)dev_mask;
    (void)os;
    (void)ec;
    std::cout << "No-op GPU reader\n";
}

error reader_gpu::impl::read(sample& s) const
{
    (void)s;
    return error::success();
}

error reader_gpu::impl::read(sample& s, int8_t ev_idx) const
{
    (void)s;
    (void)ev_idx;
    return error::success();
}

int8_t reader_gpu::impl::event_idx(uint8_t device) const
{
    (void)device;
    return -1;
}

size_t reader_gpu::impl::num_events() const
{
    return 0;
}

result<units_power> reader_gpu::impl::get_board_power(const sample& s, uint8_t dev) const
{
    (void)s;
    (void)dev;
    return error(error_code::NO_EVENT);
}

#endif


// end impl


reader_gpu::reader_gpu(uint8_t dev_mask, size_t offset, error& ec) :
    _impl(std::make_shared<reader_gpu::impl>(dev_mask, offset, ec))
{}

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
    return _impl->read(s);
}

error reader_gpu::read(sample& s, int8_t ev_idx) const
{
    return _impl->read(s, ev_idx);
}

int8_t reader_gpu::event_idx(uint8_t device) const
{
    return _impl->event_idx(device);
}

size_t reader_gpu::num_events() const
{
    return _impl->num_events();
}

result<units_power> reader_gpu::get_board_power(const sample& s, uint8_t dev) const
{
    return _impl->get_board_power(s, dev);
}
