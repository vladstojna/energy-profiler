// reader_rapl.hpp

#pragma once

#include <nrg/constants.hpp>
#include <nrg/location.hpp>
#include <nrg/reader.hpp>
#include <nrg/types.hpp>

#include <iostream>
#include <memory>
#include <vector>

namespace nrgprf
{
    class error;
    class sample;

    class reader_rapl : public reader
    {
    private:
        class impl;
        std::unique_ptr<impl> _impl;

    public:
        explicit reader_rapl(location_mask, socket_mask, error&, std::ostream & = std::cout);
        explicit reader_rapl(location_mask, error&, std::ostream & = std::cout);
        explicit reader_rapl(socket_mask, error&, std::ostream & = std::cout);
        explicit reader_rapl(error&, std::ostream & = std::cout);

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

        template<typename Location>
        result<sensor_value> value(const sample& s, uint8_t skt) const;

        template<typename Location>
        std::vector<std::pair<uint32_t, sensor_value>> values(const sample& s) const;

    private:
        const impl* pimpl() const;
        impl* pimpl();
    };
}
