#include <nrg/hybrid_reader_tp.hpp>

#include <nrg/error.hpp>

namespace nrgprf
{
    template<typename... Ts>
    hybrid_reader_tp<Ts...>::caller::caller(sample& s) :
        _sample(s)
    {}

    template<typename... Ts>
    template<typename First, typename... Rest>
    error hybrid_reader_tp<Ts...>::caller::operator()(
        const First& first,
        const Rest&... rest)
    {
        if (error err = first.read(_sample))
            return err;
        if constexpr (sizeof...(rest) > 0)
            if (error err = operator()(rest...))
                return err;
        return error::success();
    }

    template<typename... Ts>
    template<typename... Readers, std::enable_if_t<detail::all_reader_ptrs_v<Readers...>, bool>>
    hybrid_reader_tp<Ts...>::hybrid_reader_tp(Readers&&... reader) :
        _readers(std::make_tuple(std::forward<Readers>(reader)...))
    {}

    template<typename... Ts>
    template<typename T>
    const T& hybrid_reader_tp<Ts...>::get() const
    {
        return std::get<T>(_readers);
    }

    template<typename... Ts>
    template<typename T>
    T& hybrid_reader_tp<Ts...>::get()
    {
        return std::get<T>(_readers);
    }

    template<typename... Ts>
    error hybrid_reader_tp<Ts...>::read(sample& s) const
    {
        return std::apply(caller{ s }, _readers);
    }

    template<typename... Ts>
    error hybrid_reader_tp<Ts...>::read(sample&, uint8_t) const
    {
        return error(error_code::NOT_IMPL,
            "Reading specific events not supported");
    }

    template<typename... Ts>
    size_t hybrid_reader_tp<Ts...>::num_events() const
    {
        return std::apply(
            [](const Ts&... args)
            {
                return (args.num_events() + ...);
            }, _readers);
    }
}
