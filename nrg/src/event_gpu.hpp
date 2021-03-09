// event_gpu.hpp

#pragma once

#include "result.hpp"

namespace nrgprf
{

    class sample;

    class event_gpu
    {
    private:
        int8_t _board_pwr;

    public:
        event_gpu();
        event_gpu(int8_t board_pwr);

        result<long long> get_board_pwr(const sample& s) const;

        friend std::ostream& operator<<(std::ostream& os, const event_gpu& ev);
    };

    std::ostream& operator<<(std::ostream& os, const event_gpu& e);

}
