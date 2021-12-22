// reader_gpu.hpp

#pragma once

#include <nrg/reader.hpp>
#include <nrg/readings_type.hpp>
#include <nrg/types.hpp>

#include <iostream>
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
        static result<readings_type::type> support(device_mask);
        static result<readings_type::type> support();

        static result<reader_gpu> create(
            readings_type::type,
            device_mask,
            std::ostream & = std::cout);
        static result<reader_gpu> create(
            readings_type::type,
            std::ostream & = std::cout);
        static result<reader_gpu> create(
            device_mask,
            std::ostream & = std::cout);
        static result<reader_gpu> create(
            std::ostream & = std::cout);

        explicit reader_gpu(readings_type::type, device_mask, error&, std::ostream & = std::cout);
        explicit reader_gpu(readings_type::type, error&, std::ostream & = std::cout);
        explicit reader_gpu(device_mask, error&, std::ostream & = std::cout);
        explicit reader_gpu(error&, std::ostream & = std::cout);

        reader_gpu(const reader_gpu& other);
        reader_gpu& operator=(const reader_gpu& other);

        reader_gpu(reader_gpu&& other);
        reader_gpu& operator=(reader_gpu&& other);

        ~reader_gpu();

        error read(sample& s) const override;
        error read(sample& s, uint8_t ev_idx) const override;
        size_t num_events() const override;

        int8_t event_idx(readings_type::type rt, uint8_t device) const;

        result<units_power>
            get_board_power(const sample& s, uint8_t dev) const;

        result<units_energy>
            get_board_energy(const sample& s, uint8_t dev) const;

        std::vector<std::pair<uint32_t, units_power>>
            get_board_power(const sample& s) const;

        std::vector<std::pair<uint32_t, units_energy>>
            get_board_energy(const sample& s) const;

    private:
        const impl* pimpl() const;
        impl* pimpl();
    };

}
