#pragma once

#include <nrg/reader.hpp>
#include <nrg/detail/all_reader_ptrs.hpp>

#include <tuple>

namespace nrgprf
{
    class error;
    class sample;

    // owning hybrid reader
    template<typename... Ts>
    class hybrid_reader_tp : public reader
    {
    private:
        std::tuple<Ts...> _readers;

        struct caller
        {
            sample& _sample;

            caller(sample&);

            template<typename First, typename... Rest>
            error operator()(const First&, const Rest&...);
        };

    public:
        template<typename... Readers, detail::all_reader_ptrs<Readers...> = true>
        hybrid_reader_tp(Readers&&...);

        template<typename T>
        const T& get() const;
        template<typename T>
        T& get();

        error read(sample&) const override;
        error read(sample&, uint8_t) const override;
        size_t num_events() const override;
    };

    template<typename... Ts>
    hybrid_reader_tp(Ts&&...)->hybrid_reader_tp<Ts...>;
}

#include <nrg/detail/hybrid_reader_tp.inl>
