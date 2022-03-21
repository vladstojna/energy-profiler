#include "reader.hpp"
#include "funcs.hpp"

#include <nonstd/expected.hpp>

namespace nrgprf
{
    bool reader_gpu_impl::read(sample& s, uint8_t ev_idx, std::error_code& ec) const noexcept
    {
        const event& ev = events[ev_idx];
        return ev.read_func(s, ev.stride, ev.handle, ec);
    }

    bool reader_gpu_impl::read(sample& s, std::error_code& ec) const noexcept
    {
        for (size_t idx = 0; idx < events.size(); idx++)
            if (!read(s, idx, ec))
                return false;
        return true;
    }

    int8_t reader_gpu_impl::event_idx(readings_type::type rt, uint8_t device) const noexcept
    {
        return event_map[device][bitpos(rt)];
    }

    size_t reader_gpu_impl::num_events() const noexcept
    {
        return events.size();
    }
}
