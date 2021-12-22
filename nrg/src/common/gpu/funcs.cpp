#include "funcs.hpp"

#include "../../fileline.hpp"

#include <util/concat.hpp>

#include <sstream>

namespace
{
    template<typename T>
    std::string to_string(const T& item)
    {
        std::ostringstream os;
        os << item;
        return os.str();
    }
}

namespace nrgprf
{
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

    error assert_device_count(unsigned int devcount)
    {
        if (devcount > nrgprf::max_devices)
            return { error_code::TOO_MANY_DEVICES,
                cmmn::concat(
                    "Too many devices: got ",
                    std::to_string(devcount),
                    ", maximum supported is ",
                    std::to_string(nrgprf::max_devices)) };
        if (!devcount)
            return { error_code::NO_DEVICES, "No devices found" };
        return error::success();
    }
}
