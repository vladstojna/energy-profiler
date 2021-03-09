// event_gpu.cpp

#include "event_gpu.hpp"
#include "sample.hpp"

using namespace nrgprf;

event_gpu::event_gpu() :
    _board_pwr(-1)
{
}

event_gpu::event_gpu(int8_t board_pwr) :
    _board_pwr(board_pwr)
{
}

result<long long> event_gpu::get_board_pwr(const sample& s) const
{
    if (_board_pwr < 0)
        return error(error_code::NO_EVENT, "no such event for said device");
    return s.get(_board_pwr);
}

std::ostream& nrgprf::operator<<(std::ostream& os, const event_gpu& ev)
{
    os << "board_pwr = " << static_cast<int>(ev._board_pwr);
    return os;
}
