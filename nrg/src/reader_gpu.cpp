// reader_gpu.cpp

#if defined GPU_NONE
#undef GPU_NONE
#endif

#if !defined GPU_NV && !defined GPU_AMD
#define GPU_NONE
#endif

#if defined GPU_NV

#include <nrg/reader_gpu.hpp>
#include <nrg/sample.hpp>
#include <util/concat.hpp>
#include "util.hpp"

#include <iostream>
#include <cassert>
#include <stdexcept>
#include <sstream>

#include <nvml.h>

#elif defined GPU_AMD

#include <nrg/reader_gpu.hpp>
#include <nrg/sample.hpp>
#include <util/concat.hpp>
#include "util.hpp"

#include <iostream>
#include <cassert>
#include <stdexcept>
#include <sstream>

#include <rocm_smi/rocm_smi.h>

#else // GPU_NONE

#include <nrg/reader_gpu.hpp>
#include <nrg/sample.hpp>
#include <util/concat.hpp>
#include "util.hpp"

#include <cassert>
#include <iostream>

#endif // defined GPU_NV

using namespace nrgprf;

#if defined GPU_NV
using gpu_handle = nvmlDevice_t;
#elif defined GPU_AMD
using gpu_handle = uint32_t;
#endif // defined GPU_NV

// common methods
#if !defined GPU_NONE
namespace
{
    template<typename T>
    std::string to_string(const T& item)
    {
        std::ostringstream os;
        os << item;
        return os.str();
    }

    constexpr size_t bitpos(readings_type::type rt)
    {
        auto val = static_cast<std::underlying_type_t<readings_type::type>>(rt);
        size_t pos = 0;
        while ((val >>= 1) & 0x1)
            pos++;
        return pos;
    }

    std::ostream& operator<<(std::ostream& os, readings_type::type rt)
    {
        switch (rt)
        {
        case readings_type::power:
            os << "power";
            break;
        case readings_type::energy:
            os << "energy";
            break;
        }
        return os;
    }

    std::string event_added(unsigned int dev, readings_type::type rt)
    {
        return fileline(cmmn::concat(
            "added event: device ", std::to_string(dev), " ", to_string(rt), " query"
        ));
    }

    std::string event_not_supported(unsigned int dev, readings_type::type rt)
    {
        return fileline(cmmn::concat(
            "device ", std::to_string(dev),
            " does not support ", to_string(rt),
            " queries"));
    }

    std::string event_not_added(unsigned int dev, readings_type::type rt)
    {
        return fileline(cmmn::concat(
            "device ", std::to_string(dev),
            " supports ", to_string(rt),
            " queries, but not adding event due to lack of support in previous device(s)"));
    }

    error get_device_count(unsigned int& devcount);
}
#endif // !defined GPU_NONE

#if defined GPU_NV || defined GPU_AMD

struct lib_handle
{
    static result<lib_handle> create()
    {
        error err = error::success();
        lib_handle lib(err);
        if (err)
            return err;
        return lib;
    }

    lib_handle(error& ec);
    ~lib_handle();

    lib_handle(const lib_handle& other)
    {
        *this = other;
    }

    lib_handle(lib_handle&& other) :
        lib_handle(other)
    {}

    lib_handle& operator=(const lib_handle& other);

    lib_handle& operator=(lib_handle&& other)
    {
        return *this = other;
    }
};

struct reader_gpu::impl
{
    struct event
    {
        gpu_handle handle;
        size_t stride;
        error(*read_func)(sample&, size_t, gpu_handle);
    };

    static result<readings_type::type> support(device_mask devmask);

    lib_handle handle;
    std::array<std::array<int8_t, 2>, max_devices> event_map;
    std::vector<event> events;

    impl(readings_type::type rt, device_mask dmask, error& ec);

    error read(sample& s, uint8_t ev_idx) const
    {
        const event& ev = events[ev_idx];
        if (error err = ev.read_func(s, ev.stride, ev.handle))
            return err;
        return error::success();
    }

    error read(sample& s) const
    {
        for (size_t idx = 0; idx < events.size(); idx++)
            if (error err = read(s, idx))
                return err;
        return error::success();
    }

    int8_t event_idx(readings_type::type rt, uint8_t device) const
    {
        return event_map[device][bitpos(rt)];
    }

    size_t num_events() const
    {
        return events.size();
    }

    result<units_power> get_board_power(const sample& s, uint8_t dev) const;
    result<units_energy> get_board_energy(const sample& s, uint8_t dev) const;

private:
    static result<readings_type::type> support(gpu_handle handle);
    static error read_energy(sample& s, size_t stride, gpu_handle handle);
    static error read_power(sample& s, size_t stride, gpu_handle handle);

    template<readings_type::type rt, typename UnitsRead, typename ToUnits, typename S>
    result<ToUnits> get_value(const S& data, uint8_t dev) const
    {
        if (event_idx(rt, dev) < 0)
            return error(error_code::NO_EVENT);
        auto result = data[dev];
        if (!result)
            return error(error_code::NO_EVENT);
        return UnitsRead(result);
    }

    constexpr static const std::array<
        std::pair<readings_type::type, decltype(event::read_func)>, 2> type_array =
    { {
        { readings_type::power, read_power },
        { readings_type::energy, read_energy }
    } };
};

#else // GPU_NONE

struct reader_gpu::impl
{
    static result<readings_type::type> support(device_mask devmask);

    impl(readings_type::type rt, device_mask dmask, error& ec);

    error read(sample& s) const;
    error read(sample& s, uint8_t ev_idx) const;

    int8_t event_idx(readings_type::type rt, uint8_t device) const;
    size_t num_events() const;

    result<units_power> get_board_power(const sample& s, uint8_t dev) const;
    result<units_energy> get_board_energy(const sample& s, uint8_t dev) const;
};

#endif // defined GPU_NV || defined GPU_AMD

#if defined GPU_NV

namespace
{
    std::string error_str(const char* prefix, nvmlReturn_t result)
    {
        return std::string(prefix)
            .append(": ")
            .append(nvmlErrorString(result));
    }

    error get_device_count(unsigned int& devcount)
    {
        nvmlReturn_t result = nvmlDeviceGetCount(&devcount);
        if (result != NVML_SUCCESS)
            return { error_code::READER_GPU, error_str("Failed to obtain device count", result) };
        if (devcount > max_devices)
            return { error_code::TOO_MANY_DEVICES, "Too many devices (a maximum of 8 is supported)" };
        if (!devcount)
            return { error_code::NO_DEVICES, "No devices found" };
        return error::success();
    }
}

lib_handle::lib_handle(error& ec)
{
    if (ec)
        return;
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

lib_handle& lib_handle::operator=(const lib_handle&)
{
    nvmlReturn_t result = nvmlInit();
    if (result != NVML_SUCCESS)
        throw std::runtime_error(error_str("failed to initialise NVML", result));
    return *this;
}

reader_gpu::impl::impl(readings_type::type rt, device_mask dev_mask, error& ec) :
    handle(ec),
    event_map(),
    events()
{
    // error constructing lib_handle
    if (ec)
        return;
    if (dev_mask.none() && (ec = { error_code::NO_DEVICES, "No devices set in mask" }))
        return;
    for (auto& val : event_map)
        val.fill(-1);

    auto sup = support(dev_mask);
    if (!sup && (ec = std::move(sup.error())))
        return;
    unsigned int device_cnt;
    if (ec = get_device_count(device_cnt))
        return;
    for (unsigned int i = 0; i < device_cnt; i++)
    {
        if (!dev_mask[i])
            continue;

        constexpr const size_t sz = NVML_DEVICE_NAME_BUFFER_SIZE;
        char name[sz];
        nvmlDevice_t handle = nullptr;
        if (nvmlReturn_t res; (res = nvmlDeviceGetHandleByIndex(i, &handle)) != NVML_SUCCESS)
        {
            ec = { error_code::READER_GPU, error_str("Failed to get device handle", res) };
            return;
        }
        if (nvmlReturn_t res; (res = nvmlDeviceGetName(handle, name, sz)) != NVML_SUCCESS)
        {
            ec = { error_code::READER_GPU, error_str("Failed to get device name", res) };
            return;
        }
        std::cerr << fileline(cmmn::concat("device: ", std::to_string(i), ", name: ", name, "\n"));

        auto sup_dev = support(handle);
        if (!sup_dev && (ec = std::move(sup_dev.error())))
            return;
        for (const auto& elem : type_array)
        {
            if (!(elem.first & rt))
                continue;
            if (!(sup_dev.value() & elem.first))
                std::cerr << event_not_supported(i, elem.first) << "\n";
            else if (!(sup.value() & elem.first))
                std::cerr << event_not_added(i, elem.first) << "\n";
            else
            {
                event_map[i][bitpos(elem.first)] = events.size();
                events.push_back({ handle, i, elem.second });
                std::cerr << event_added(i, elem.first) << "\n";
            }
        }
    }
    if (events.empty())
        ec = { error_code::SETUP_ERROR, "No events were added" };
}

result<readings_type::type> reader_gpu::impl::support(device_mask devmask)
{
    if (devmask.none())
        return error(error_code::NO_DEVICES, "No devices set in mask");
    auto lib = lib_handle::create();
    if (!lib)
        return std::move(lib.error());
    unsigned int devcount;
    if (error err = get_device_count(devcount))
        return err;
    readings_type::type retval = readings_type::all;
    for (unsigned i = 0; i < devcount; i++)
    {
        nvmlDevice_t devhandle = nullptr;
        if (!devmask[i])
            continue;
        if (nvmlReturn_t res; (res = nvmlDeviceGetHandleByIndex(i, &devhandle)) != NVML_SUCCESS)
            return error(error_code::SETUP_ERROR, error_str("Failed to get device handle", res));
        if (auto sup = support(devhandle))
            retval = retval & sup.value();
        else
            return std::move(sup.error());
    }
    if (!retval)
        return error(error_code::UNSUPPORTED, "Both power and energy are unsupported");
    return retval;
}

result<readings_type::type> reader_gpu::impl::support(nvmlDevice_t handle)
{
    assert(handle);
    nvmlReturn_t res;
    readings_type::type rt = readings_type::all;
    unsigned int power;
    if ((res = nvmlDeviceGetPowerUsage(handle, &power)) == NVML_ERROR_NOT_SUPPORTED)
        rt = rt ^ readings_type::power;
    else if (res != NVML_SUCCESS)
        return error(error_code::READER_GPU, error_str("Cannot query support", res));
    unsigned long long energy;
    if ((res = nvmlDeviceGetTotalEnergyConsumption(handle, &energy)) == NVML_ERROR_NOT_SUPPORTED)
        rt = rt ^ readings_type::energy;
    else if (res != NVML_SUCCESS)
        return error(error_code::READER_GPU, error_str("Cannot query support", res));
    return rt;
}

error reader_gpu::impl::read_energy(sample& s, size_t stride, nvmlDevice_t handle)
{
    unsigned long long energy;
    nvmlReturn_t result = nvmlDeviceGetTotalEnergyConsumption(handle, &energy);
    if (result != NVML_SUCCESS)
        return { error_code::READER_GPU, nvmlErrorString(result) };
    s.data.gpu_energy[stride] = energy;
    return error::success();
}

error reader_gpu::impl::read_power(sample& s, size_t stride, nvmlDevice_t handle)
{
    unsigned int power;
    nvmlReturn_t result = nvmlDeviceGetPowerUsage(handle, &power);
    if (result != NVML_SUCCESS)
        return { error_code::READER_GPU, nvmlErrorString(result) };
    s.data.gpu_power[stride] = power;
    return error::success();
}

result<units_power> reader_gpu::impl::get_board_power(const sample& s, uint8_t dev) const
{
    return get_value<
        readings_type::power,
        milliwatts<uint32_t>,
        units_power
    >(s.data.gpu_power, dev);
}

result<units_energy> reader_gpu::impl::get_board_energy(const sample& s, uint8_t dev) const
{
    return get_value<
        readings_type::energy,
        millijoules<uint32_t>,
        units_energy
    >(s.data.gpu_energy, dev);
}

#elif defined GPU_AMD

namespace
{
    std::string error_str(const char* prefix, rsmi_status_t result)
    {
        const char* str;
        rsmi_status_string(result, &str);
        return std::string(prefix)
            .append(": ")
            .append(str);
    }

    error get_device_count(unsigned int& devcount)
    {
        rsmi_status_t result = rsmi_num_monitor_devices(&devcount);
        if (result != RSMI_STATUS_SUCCESS)
            return { error_code::READER_GPU, error_str("Failed to obtain device count", result) };
        if (devcount > max_devices)
            return { error_code::TOO_MANY_DEVICES, "Too many devices (a maximum of 8 is supported)" };
        if (!devcount)
            return { error_code::NO_DEVICES, "No devices found" };
        return error::success();
    }
}

lib_handle::lib_handle(error& ec)
{
    if (ec)
        return;
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

lib_handle& lib_handle::operator=(const lib_handle&)
{
    rsmi_status_t result = rsmi_init(0);
    if (result != RSMI_STATUS_SUCCESS)
        throw std::runtime_error(error_str("failed to initialise ROCm SMI", result));
    return *this;
}

reader_gpu::impl::impl(readings_type::type rt, device_mask dev_mask, error& ec) :
    handle(ec),
    event_map(),
    events()
{
    // error constructing lib_handle
    if (ec)
        return;
    if (dev_mask.none() && (ec = { error_code::NO_DEVICES, "No devices set in mask" }))
        return;
    for (auto& val : event_map)
        val.fill(-1);

    rsmi_version_t version;
    if (rsmi_status_t res; (res = rsmi_version_get(&version)) != RSMI_STATUS_SUCCESS)
    {
        ec = { error_code::READER_GPU, error_str("Failed to obtain lib build version", res) };
        return;
    }

    std::cerr << fileline("ROCm SMI version info: ");
    std::cerr << "major: " << version.major << ", minor: " << version.minor
        << ", patch: " << version.patch << ", build: " << version.build << "\n";

    auto sup = support(dev_mask);
    if (!sup && (ec = std::move(sup.error())))
        return;
    unsigned int device_cnt;
    if (ec = get_device_count(device_cnt))
        return;
    for (uint32_t dev_idx = 0; dev_idx < device_cnt; dev_idx++)
    {
        if (!dev_mask[dev_idx])
            continue;

        char name[512];
        uint64_t pciid;
        if (rsmi_status_t res = rsmi_dev_pci_id_get(dev_idx, &pciid); res != RSMI_STATUS_SUCCESS)
        {
            ec = { error_code::READER_GPU, error_str("Failed to get device PCI ID", res) };
            return;
        }
        if (rsmi_status_t res = rsmi_dev_name_get(dev_idx, name, sizeof(name));
            res != RSMI_STATUS_SUCCESS && res != RSMI_STATUS_INSUFFICIENT_SIZE)
        {
            ec = { error_code::READER_GPU, error_str("Failed to get device name", res) };
            return;
        }
        std::cerr << fileline("")
            << "idx: " << dev_idx
            << ", PCI id: " << pciid
            << ", name: " << name << "\n";

        auto sup_dev = support(dev_idx);
        if (!sup_dev && (ec = std::move(sup_dev.error())))
            return;
        for (const auto& elem : type_array)
        {
            if (!(elem.first & rt))
                continue;
            if (!(sup_dev.value() & elem.first))
                std::cerr << event_not_supported(dev_idx, elem.first) << "\n";
            else if (!(sup.value() & elem.first))
                std::cerr << event_not_added(dev_idx, elem.first) << "\n";
            else
            {
                event_map[dev_idx][bitpos(elem.first)] = events.size();
                events.push_back({ dev_idx, dev_idx, elem.second });
                std::cerr << event_added(dev_idx, elem.first) << "\n";
            }
        }
    }
    if (events.empty())
        ec = { error_code::SETUP_ERROR, "No events were added" };
}

result<readings_type::type> reader_gpu::impl::support(device_mask devmask)
{
    if (devmask.none())
        return error(error_code::NO_DEVICES, "No devices set in mask");
    auto lib = lib_handle::create();
    if (!lib)
        return std::move(lib.error());
    unsigned int devcount;
    if (error err = get_device_count(devcount))
        return err;
    readings_type::type retval = readings_type::all;
    for (unsigned i = 0; i < devcount; i++)
    {
        if (!devmask[i])
            continue;
        if (auto sup = support(i))
            retval = retval & sup.value();
        else
            return std::move(sup.error());
    }
    if (!retval)
        return error(error_code::UNSUPPORTED, "Both power and energy are unsupported");
    return retval;
}

result<readings_type::type> reader_gpu::impl::support(gpu_handle h)
{
    rsmi_status_t res;
    readings_type::type rt = readings_type::all;
    if (uint64_t power; (res = rsmi_dev_power_ave_get(h, 0, &power)) == RSMI_STATUS_NOT_SUPPORTED)
        rt = rt ^ readings_type::power;
    else if (res != RSMI_STATUS_SUCCESS)
        return error(error_code::READER_GPU, error_str("Cannot query support", res));
    return rt ^ readings_type::energy;
}

error reader_gpu::impl::read_energy(sample&, size_t, gpu_handle)
{
    return error(error_code::UNSUPPORTED, "Energy readings are not supported");
}

error reader_gpu::impl::read_power(sample& s, size_t stride, gpu_handle handle)
{
    uint64_t power;
    rsmi_status_t result = rsmi_dev_power_ave_get(handle, 0, &power);
    if (result != RSMI_STATUS_SUCCESS)
    {
        const char* str;
        rsmi_status_string(result, &str);
        return { error_code::READER_GPU, str };
    }
    s.data.gpu_power[stride] = power;
    return error::success();
}

result<units_power> reader_gpu::impl::get_board_power(const sample& s, uint8_t dev) const
{
    return get_value<
        readings_type::power,
        microwatts<uint32_t>,
        units_power
    >(s.data.gpu_power, dev);
}

result<units_energy> reader_gpu::impl::get_board_energy(const sample&, uint8_t) const
{
    return error(error_code::NO_EVENT);
}

#else // GPU_NONE

reader_gpu::impl::impl(readings_type::type, device_mask, error&)
{
    std::cerr << fileline("No-op GPU reader\n");
}

int8_t reader_gpu::impl::event_idx(readings_type::type, uint8_t) const
{
    return -1;
}

error reader_gpu::impl::read(sample&) const
{
    return error::success();
}

error reader_gpu::impl::read(sample&, uint8_t) const
{
    return error::success();
}

size_t reader_gpu::impl::num_events() const
{
    return 0;
}

result<units_power> reader_gpu::impl::get_board_power(const sample&, uint8_t) const
{
    return error(error_code::NO_EVENT);
}

result<units_energy> reader_gpu::impl::get_board_energy(const sample&, uint8_t) const
{
    return error(error_code::NO_EVENT);
}

result<readings_type::type> reader_gpu::impl::support(device_mask)
{
    return static_cast<readings_type::type>(0);
}

#endif // defined GPU_NV


readings_type::type readings_type::operator|(readings_type::type lhs, readings_type::type rhs)
{
    return static_cast<readings_type::type>(
        static_cast<std::underlying_type_t<readings_type::type>>(lhs) |
        static_cast<std::underlying_type_t<readings_type::type>>(rhs)
        );
}

readings_type::type readings_type::operator&(readings_type::type lhs, readings_type::type rhs)
{
    return static_cast<readings_type::type>(
        static_cast<std::underlying_type_t<readings_type::type>>(lhs) &
        static_cast<std::underlying_type_t<readings_type::type>>(rhs)
        );
}

readings_type::type readings_type::operator^(readings_type::type lhs, readings_type::type rhs)
{
    return static_cast<readings_type::type>(
        static_cast<std::underlying_type_t<readings_type::type>>(lhs) ^
        static_cast<std::underlying_type_t<readings_type::type>>(rhs)
        );
}

const readings_type::type readings_type::all = readings_type::power | readings_type::energy;

result<readings_type::type> reader_gpu::support(device_mask devmask)
{
    return impl::support(devmask);
}

result<readings_type::type> reader_gpu::support()
{
    return support(device_mask(0xff));
}

reader_gpu::reader_gpu(readings_type::type rt, device_mask dev_mask, error& ec) :
    _impl(std::make_unique<reader_gpu::impl>(rt, dev_mask, ec))
{}

reader_gpu::reader_gpu(readings_type::type rt, error& ec) :
    reader_gpu(rt, device_mask(0xff), ec)
{}

reader_gpu::reader_gpu(device_mask dev_mask, error& ec) :
    reader_gpu(readings_type::all, dev_mask, ec)
{}

reader_gpu::reader_gpu(error& ec) :
    reader_gpu(readings_type::all, device_mask(0xff), ec)
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

int8_t reader_gpu::event_idx(readings_type::type rt, uint8_t device) const
{
    return pimpl()->event_idx(rt, device);
}

size_t reader_gpu::num_events() const
{
    return pimpl()->num_events();
}

result<units_power> reader_gpu::get_board_power(const sample & s, uint8_t dev) const
{
    return pimpl()->get_board_power(s, dev);
}

result<units_energy> reader_gpu::get_board_energy(const sample & s, uint8_t dev) const
{
    return pimpl()->get_board_energy(s, dev);
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

#define DEFINE_GET_METHOD(rettype, method) \
std::vector<std::pair<uint32_t, rettype>> \
reader_gpu::method(const sample& s) const \
{ \
    std::vector<std::pair<uint32_t, rettype>> retval; \
    for (uint32_t d = 0; d < max_devices; d++) \
    { \
        if (auto val = method(s, d)) \
            retval.push_back({ d, std::move(val.value()) }); \
    } \
    return retval; \
}

DEFINE_GET_METHOD(units_power, get_board_power)
DEFINE_GET_METHOD(units_energy, get_board_energy)
