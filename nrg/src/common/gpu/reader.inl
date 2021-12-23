#include "reader.hpp"

template<
    nrgprf::readings_type::type rt,
    typename UnitsRead,
    typename ToUnits,
    typename S
>
nrgprf::result<ToUnits>
nrgprf::reader_gpu_impl::get_value(const S& data, uint8_t dev) const
{
    if (event_idx(rt, dev) < 0)
        return result<ToUnits>(nonstd::unexpect, error_code::NO_EVENT);
    auto res = data[dev];
    if (!res)
        return result<ToUnits>(nonstd::unexpect, error_code::NO_EVENT);
    return UnitsRead(res);
}
