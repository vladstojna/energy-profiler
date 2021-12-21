#include "funcs.hpp"

#include "../../util.hpp"

#include <util/concat.hpp>

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
}
