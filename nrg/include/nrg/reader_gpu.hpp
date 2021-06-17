// reader_gpu.hpp

#pragma once

#include <nrg/reader.hpp>
#include <nrg/types.hpp>

#include <memory>
#include <vector>

namespace nrgprf
{

    class error;
    class sample;

    class reader_gpu : public reader
    {
    private:
        struct impl;
        std::unique_ptr<impl> _impl;

    public:
        struct dev_pwr
        {
            uint32_t dev;
            units_power power;
        };

        reader_gpu(error&);
        reader_gpu(device_mask, error&);

        reader_gpu(const reader_gpu& other);
        reader_gpu& operator=(const reader_gpu& other);

        reader_gpu(reader_gpu&& other);
        reader_gpu& operator=(reader_gpu&& other);

        ~reader_gpu();

        error read(sample& s) const override;
        error read(sample& s, uint8_t ev_idx) const override;
        size_t num_events() const override;

        int8_t event_idx(uint8_t device) const;

        result<units_power> get_board_power(const sample& s, uint8_t dev) const;

        std::vector<dev_pwr> get_board_power(const sample& s) const;

    private:
        const impl* pimpl() const;
        impl* pimpl();
    };

}
