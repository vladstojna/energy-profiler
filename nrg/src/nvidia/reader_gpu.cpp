#include "../common/gpu/reader.hpp"
#include "../common/gpu/funcs.hpp"
#include "../util.hpp"

#include <nrg/sample.hpp>

#include <nonstd/expected.hpp>

#include <nvml.h>

#include <cassert>
#include <iostream>
#include <stdexcept>

namespace
{
    std::string error_str(const char* prefix, nvmlReturn_t result)
    {
        return std::string(prefix)
            .append(": ")
            .append(nvmlErrorString(result));
    }

    nrgprf::result<unsigned int> get_device_count()
    {
        using nrgprf::error;
        using nrgprf::error_code;
        using rettype = decltype(get_device_count());
        unsigned int devcount;
        nvmlReturn_t result = nvmlDeviceGetCount(&devcount);
        if (result != NVML_SUCCESS)
            return rettype(nonstd::unexpect,
                error_code::READER_GPU,
                error_str("Failed to obtain device count", result));
        if (error err = nrgprf::assert_device_count(devcount))
            return rettype(nonstd::unexpect, std::move(err));
        return devcount;
    }
}

namespace nrgprf
{
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

    lib_handle::lib_handle(const lib_handle& other)
    {
        *this = other;
    }

    lib_handle::lib_handle(lib_handle&& other) :
        lib_handle(other)
    {}

    lib_handle& lib_handle::operator=(const lib_handle&)
    {
        nvmlReturn_t result = nvmlInit();
        if (result != NVML_SUCCESS)
            throw std::runtime_error(error_str("failed to initialise NVML", result));
        return *this;
    }

    lib_handle& lib_handle::operator=(lib_handle&& other)
    {
        return *this = other;
    }

    reader_gpu_impl::reader_gpu_impl(
        readings_type::type rt,
        device_mask dev_mask,
        error& ec,
        std::ostream& os)
        :
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
        auto device_cnt = get_device_count();
        if (!device_cnt && (ec = std::move(device_cnt.error())))
            return;
        for (unsigned int i = 0; i < *device_cnt; i++)
        {
            if (!dev_mask[i])
                continue;

            constexpr size_t sz = NVML_DEVICE_NAME_BUFFER_SIZE;
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
            os << fileline("device: ") << i << ", name: " << name << "\n";
            auto sup_dev = support(handle);
            if (!sup_dev && (ec = std::move(sup_dev.error())))
                return;
            for (const auto& elem : type_array)
            {
                if (!(elem.first & rt))
                    continue;
                if (!(*sup_dev & elem.first))
                    os << event_not_supported(i, elem.first) << "\n";
                else if (!(*sup & elem.first))
                    os << event_not_added(i, elem.first) << "\n";
                else
                {
                    event_map[i][bitpos(elem.first)] = events.size();
                    events.push_back({ handle, i, elem.second });
                    os << event_added(i, elem.first) << "\n";
                }
            }
        }
        if (events.empty())
            ec = { error_code::SETUP_ERROR, "No events were added" };
    }

    result<readings_type::type> reader_gpu_impl::support(device_mask devmask)
    {
        using rettype = result<readings_type::type>;
        if (devmask.none())
            return rettype(nonstd::unexpect, error_code::NO_DEVICES, "No devices set in mask");
        auto lib = lib_handle::create();
        if (!lib)
            return rettype(nonstd::unexpect, std::move(lib.error()));
        auto devcount = get_device_count();
        if (!devcount)
            return rettype(nonstd::unexpect, std::move(devcount).error());
        readings_type::type retval = readings_type::all;
        for (unsigned i = 0; i < *devcount; i++)
        {
            nvmlDevice_t devhandle = nullptr;
            if (!devmask[i])
                continue;
            if (nvmlReturn_t res; (res = nvmlDeviceGetHandleByIndex(i, &devhandle)) != NVML_SUCCESS)
                return rettype(nonstd::unexpect,
                    error_code::SETUP_ERROR,
                    error_str("Failed to get device handle", res));
            if (auto sup = support(devhandle))
                retval = retval & *sup;
            else
                return sup;
        }
        if (!retval)
            return rettype(nonstd::unexpect,
                error_code::UNSUPPORTED,
                "Both power and energy are unsupported");
        return retval;
    }

    result<readings_type::type> reader_gpu_impl::support(nvmlDevice_t handle)
    {
        assert(handle);
        auto cannot_query_support = [](nvmlReturn_t res)
        {
            return result<readings_type::type>(nonstd::unexpect,
                error_code::READER_GPU,
                error_str("Cannot query support", res));
        };

        nvmlReturn_t res;
        readings_type::type rt = readings_type::all;
        unsigned int power;
        if ((res = nvmlDeviceGetPowerUsage(handle, &power)) == NVML_ERROR_NOT_SUPPORTED)
            rt = rt ^ readings_type::power;
        else if (res != NVML_SUCCESS)
            return cannot_query_support(res);
        unsigned long long energy;
        if ((res = nvmlDeviceGetTotalEnergyConsumption(handle, &energy)) == NVML_ERROR_NOT_SUPPORTED)
            rt = rt ^ readings_type::energy;
        else if (res != NVML_SUCCESS)
            return cannot_query_support(res);
        return rt;
    }

    error reader_gpu_impl::read_energy(sample& s, size_t stride, nvmlDevice_t handle)
    {
        unsigned long long energy;
        nvmlReturn_t result = nvmlDeviceGetTotalEnergyConsumption(handle, &energy);
        if (result != NVML_SUCCESS)
            return { error_code::READER_GPU, nvmlErrorString(result) };
        s.data.gpu_energy[stride] = energy;
        return error::success();
    }

    error reader_gpu_impl::read_power(sample& s, size_t stride, nvmlDevice_t handle)
    {
        unsigned int power;
        nvmlReturn_t result = nvmlDeviceGetPowerUsage(handle, &power);
        if (result != NVML_SUCCESS)
            return { error_code::READER_GPU, nvmlErrorString(result) };
        s.data.gpu_power[stride] = power;
        return error::success();
    }

    result<units_power> reader_gpu_impl::get_board_power(const sample& s, uint8_t dev) const
    {
        return get_value<
            readings_type::power,
            milliwatts<uint32_t>,
            units_power
        >(s.data.gpu_power, dev);
    }

    result<units_energy> reader_gpu_impl::get_board_energy(const sample& s, uint8_t dev) const
    {
        return get_value<
            readings_type::energy,
            millijoules<uint32_t>,
            units_energy
        >(s.data.gpu_energy, dev);
    }
}

#include "../common/gpu/reader.inl"
