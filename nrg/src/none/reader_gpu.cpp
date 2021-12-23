#include "../fileline.hpp"
#include "reader_gpu.hpp"

#include <nonstd/expected.hpp>

namespace nrgprf
{
    reader_gpu_impl::reader_gpu_impl(
        readings_type::type, device_mask, error&, std::ostream& os)
    {
        os << fileline("No-op GPU reader\n");
    }

    int8_t reader_gpu_impl::event_idx(readings_type::type, uint8_t) const
    {
        return -1;
    }

    error reader_gpu_impl::read(sample&) const
    {
        return error::success();
    }

    error reader_gpu_impl::read(sample&, uint8_t) const
    {
        return error::success();
    }

    size_t reader_gpu_impl::num_events() const
    {
        return 0;
    }

    result<units_power> reader_gpu_impl::get_board_power(const sample&, uint8_t) const
    {
        return result<units_power>(nonstd::unexpect, error_code::NO_EVENT);
    }

    result<units_energy> reader_gpu_impl::get_board_energy(const sample&, uint8_t) const
    {
        return result<units_energy>(nonstd::unexpect, error_code::NO_EVENT);
    }

    result<readings_type::type> reader_gpu_impl::support(device_mask)
    {
        return static_cast<readings_type::type>(0);
    }
}
