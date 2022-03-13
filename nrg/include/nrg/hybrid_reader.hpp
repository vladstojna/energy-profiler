// hybrid_reader.hpp
#pragma once

#include <nrg/reader.hpp>
#include <nrg/types.hpp>
#include <nrg/detail/all_reader_ptrs.hpp>

#include <vector>

namespace nrgprf
{
    class error;
    class sample;

    // non-owning hybrid reader
    class hybrid_reader : public reader
    {
    private:
        std::vector<const reader*> _readers;

    public:
        template<
            typename... Readers,
            std::enable_if_t<detail::all_reader_ptrs_v<Readers...>, bool> = true
        > hybrid_reader(const Readers&...);

        void push_back(const reader&);

        bool read(sample&, std::error_code&) const override;
        bool read(sample&, uint8_t, std::error_code&) const override;
        size_t num_events() const noexcept override;
    };

    template<
        typename... Readers,
        std::enable_if_t<detail::all_reader_ptrs_v<Readers...>, bool>
    > hybrid_reader::hybrid_reader(const Readers&... reader) :
        _readers({ &reader... })
    {}
}
