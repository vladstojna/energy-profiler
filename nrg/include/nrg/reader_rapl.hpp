// reader_rapl.hpp

#pragma once

#include <nrg/constants.hpp>
#include <nrg/reader.hpp>
#include <nrg/types.hpp>

#include <memory>
#include <vector>
#include <type_traits>

namespace nrgprf
{

    class error;
    class sample;

    class reader_rapl : public reader
    {
    public:
        struct package : std::integral_constant<int32_t, 0> {};
        struct cores : std::integral_constant<int32_t, 1> {};
        struct uncore : std::integral_constant<int32_t, 2> {};
        struct dram : std::integral_constant<int32_t, 3> {};

        struct skt_energy
        {
            uint32_t skt;
            units_energy energy;
        };

    private:
        class impl;
        std::unique_ptr<impl> _impl;

    public:
        reader_rapl(rapl_mask, socket_mask, error&);
        reader_rapl(rapl_mask, error&);
        reader_rapl(socket_mask, error&);
        reader_rapl(error&);

        reader_rapl(const reader_rapl& other);
        reader_rapl& operator=(const reader_rapl& other);

        reader_rapl(reader_rapl&& other);
        reader_rapl& operator=(reader_rapl&& other);

        ~reader_rapl();

        error read(sample& s) const override;
        error read(sample& s, uint8_t ev_idx) const override;
        size_t num_events() const override;

        template<typename Tag>
        int32_t event_idx(uint8_t skt) const;

        template<typename Tag>
        result<units_energy> get_energy(const sample& s, uint8_t skt) const;

        template<typename Tag>
        std::vector<skt_energy> get_energy(const sample& s) const;

    private:
        const impl* pimpl() const;
        impl* pimpl();
    };

}
