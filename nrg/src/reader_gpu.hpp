// reader_gpu.hpp

#pragma once

#include "error.hpp"
#include "reader_rapl.hpp"
#include "sample.hpp"

#include <memory>

namespace nrgprf
{

    class reader_gpu
    {
    private:
        struct impl;
        std::shared_ptr<impl> _impl;

    public:
        reader_gpu(error& ec);
        reader_gpu(const reader_rapl& reader, error& ec);
        reader_gpu(uint8_t dev_mask, error& ec);
        reader_gpu(uint8_t dev_mask, const reader_rapl& reader, error& ec);

        error read(sample& s) const;
        error read(sample& s, int8_t ev_idx) const;

        int8_t event_idx(uint8_t device) const;
        size_t num_events() const;

        result<uint64_t> get_board_power(const sample& s, uint8_t dev) const;

    private:
        reader_gpu(uint8_t dev_mask, size_t offset, error& ec);
    };

}
