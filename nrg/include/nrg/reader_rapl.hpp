// reader_rapl.hpp

#pragma once

#include <nrg/constants.hpp>
#include <nrg/reader.hpp>
#include <nrg/types.hpp>

#include <memory>
#include <vector>

namespace nrgprf
{

    namespace loc
    {
        struct sys;
        struct pkg;
        struct cores;
        struct uncore;
        struct nest;
        struct mem;
        struct gpu;
    }

    class error;
    class sample;

    class reader_rapl : public reader
    {
    private:
        class impl;
        std::unique_ptr<impl> _impl;

    public:
        reader_rapl(location_mask, socket_mask, error&);
        reader_rapl(location_mask, error&);
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

        template<typename Location, typename Type>
        result<Type> value(const sample& s, uint8_t skt) const;

        template<typename Location, typename Type>
        std::vector<std::pair<uint32_t, Type>> values(const sample& s) const;

    private:
        const impl* pimpl() const;
        impl* pimpl();
    };

}
