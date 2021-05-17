// reader_gpu.hpp

#pragma once

#include <nrg/reader.hpp>
#include <nrg/types.hpp>

#include <memory>

namespace nrgprf
{

    class error;
    class sample;

    class reader_gpu : public reader
    {
    private:
        struct impl;
        std::shared_ptr<impl> _impl;

    public:
        reader_gpu(error& ec);
        reader_gpu(uint8_t dev_mask, error& ec);

        error read(sample& s) const override;
        error read(sample& s, uint8_t ev_idx) const override;
        size_t num_events() const override;

        int8_t event_idx(uint8_t device) const;

        result<units_power> get_board_power(const sample& s, uint8_t dev) const;

    private:
        reader_gpu(uint8_t dev_mask, size_t offset, error& ec);
    };

}
