// hybrid_reader.hpp
#pragma once

#include <nrg/error.hpp>
#include <nrg/reader.hpp>

#include <vector>
#include <tuple>

namespace nrgprf
{

    namespace detail
    {
        template<typename... Ts>
        using all_reader_ptrs = typename std::enable_if<
            std::conjunction<std::is_convertible<Ts*, reader*>...>::value, bool>::type;
    }

    // owning hybrid reader

    template<typename... Ts>
    class hybrid_reader_tp : public reader
    {
    private:
        std::tuple<Ts...> _readers;

        struct caller
        {
            sample& _sample;

            caller(sample& s) :
                _sample(s)
            {}

            template<typename First, typename... Rest>
            error operator()(const First& first, const Rest&... rest)
            {
                error err = first.read(_sample);
                if (err)
                    return err;
                if constexpr (sizeof...(rest) > 0)
                {
                    err = operator()(rest...);
                    if (err)
                        return err;
                }
                return error::success();
            }
        };

    public:
        template<typename... Readers, detail::all_reader_ptrs<Readers...> = true>
        hybrid_reader_tp(Readers&&... reader) :
            _readers(std::make_tuple(std::forward<Readers>(reader)...))
        {}

        template<typename T>
        const T& get() const
        {
            return std::get<T>(_readers);
        }

        error read(sample& s) const override
        {
            return std::apply(caller{ s }, _readers);
        }

        error read(sample&, uint8_t ev_idx) const override
        {
            return error(error_code::NOT_IMPL, "Reading specific events not supported");
        }

        size_t num_events() const override
        {
            return std::apply(
                [](const Ts&... args)
                {
                    return (args.num_events() + ...);
                }, _readers);
        }
    };

    // non-owning hybrid reader

    class hybrid_reader : public reader
    {
    private:
        std::vector<const reader*> _readers;

    public:
        template<typename... Readers, detail::all_reader_ptrs<Readers...> = true>
        hybrid_reader(const Readers&... reader) :
            _readers({ &reader... })
        {}

        void push_back(const reader& r);

        error read(sample&) const override;
        error read(sample&, uint8_t ev_idx) const override;
        size_t num_events() const override;
    };


    // deduction guides

    template<typename... Ts>
    hybrid_reader_tp(Ts&&...)->hybrid_reader_tp<Ts...>;

}
