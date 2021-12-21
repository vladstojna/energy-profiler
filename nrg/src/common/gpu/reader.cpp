#include "reader.hpp"
#include "funcs.hpp"

#include <nonstd/expected.hpp>

namespace nrgprf
{
    result<lib_handle> lib_handle::create()
    {
        error err = error::success();
        lib_handle lib(err);
        if (err)
            return err;
        return lib;
    }

    error reader_gpu_impl::read(sample& s, uint8_t ev_idx) const
    {
        const event& ev = events[ev_idx];
        if (error err = ev.read_func(s, ev.stride, ev.handle))
            return err;
        return error::success();
    }

    error reader_gpu_impl::read(sample& s) const
    {
        for (size_t idx = 0; idx < events.size(); idx++)
            if (error err = read(s, idx))
                return err;
        return error::success();
    }

    int8_t reader_gpu_impl::event_idx(readings_type::type rt, uint8_t device) const
    {
        return event_map[device][bitpos(rt)];
    }

    size_t reader_gpu_impl::num_events() const
    {
        return events.size();
    }
}
