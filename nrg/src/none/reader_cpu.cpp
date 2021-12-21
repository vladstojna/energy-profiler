#include "../util.hpp"
#include "reader_cpu.hpp"

#include <nrg/location.hpp>
#include <nonstd/expected.hpp>

namespace nrgprf
{
    reader_impl::reader_impl(
        location_mask, socket_mask, error&, std::ostream& os)
    {
        os << fileline("No-op CPU reader\n");
    }

    error reader_impl::read(sample&) const
    {
        return error::success();
    }

    error reader_impl::read(sample&, uint8_t) const
    {
        return error::success();
    }

    size_t reader_impl::num_events() const
    {
        return 0;
    }

    template<typename Location>
    int32_t reader_impl::event_idx(uint8_t) const
    {
        return -1;
    }

    template<typename Location>
    result<sensor_value> reader_impl::value(const sample&, uint8_t) const
    {
        return result<sensor_value>(nonstd::unexpect, error_code::NO_EVENT);
    }
}

#include "../instantiate.hpp"
INSTANTIATE_ALL(nrgprf::reader_impl, INSTANTIATE_EVENT_IDX);
INSTANTIATE_ALL(nrgprf::reader_impl, INSTANTIATE_VALUE);
