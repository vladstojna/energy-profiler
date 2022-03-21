#include "../fileline.hpp"
#include "../gpu_category.hpp"
#include "reader_gpu.hpp"

#include <nonstd/expected.hpp>

namespace nrgprf
{
    std::string gpu_category_t::message(int) const
    {
        return "(unrecognized error code)";
    }

    reader_gpu_impl::reader_gpu_impl(
        readings_type::type, device_mask, std::ostream& os)
    {
        os << fileline("No-op GPU reader\n");
    }

    int8_t reader_gpu_impl::event_idx(readings_type::type, uint8_t) const noexcept
    {
        return -1;
    }

    bool reader_gpu_impl::read(sample&, std::error_code&) const noexcept
    {
        return true;
    }

    bool reader_gpu_impl::read(sample&, uint8_t, std::error_code&) const noexcept
    {
        return true;
    }

    size_t reader_gpu_impl::num_events() const noexcept
    {
        return 0;
    }

    result<units_power> reader_gpu_impl::get_board_power(const sample&, uint8_t) const noexcept
    {
        return result<units_power>(nonstd::unexpect, errc::no_such_event);
    }

    result<units_energy> reader_gpu_impl::get_board_energy(const sample&, uint8_t) const noexcept
    {
        return result<units_energy>(nonstd::unexpect, errc::no_such_event);
    }

    result<readings_type::type> reader_gpu_impl::support(device_mask) noexcept
    {
        return static_cast<readings_type::type>(0);
    }
}
