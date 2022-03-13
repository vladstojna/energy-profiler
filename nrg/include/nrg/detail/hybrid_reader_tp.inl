#include <nrg/hybrid_reader_tp.hpp>

#include <nrg/error.hpp>
#include <nrg/sample.hpp>

#include <nonstd/expected.hpp>

namespace nrgprf
{
    template<typename... Ts>
    hybrid_reader_tp<Ts...>::caller::caller(sample& s, std::error_code& ec) :
        _sample(s),
        _ec(ec)
    {}

    template<typename... Ts>
    template<typename First, typename... Rest>
    bool hybrid_reader_tp<Ts...>::caller::operator()(
        const First& first,
        const Rest&... rest)
    {
        if (!first.read(_sample, _ec))
            return false;
        if constexpr (sizeof...(rest) > 0)
            if (!operator()(rest...))
                return false;
        return true;
    }

    template<typename... Ts>
    template<typename... Readers, std::enable_if_t<detail::all_reader_ptrs_v<Readers...>, bool>>
    hybrid_reader_tp<Ts...>::hybrid_reader_tp(Readers&&... reader) :
        _readers(std::make_tuple(std::forward<Readers>(reader)...))
    {}

    template<typename... Ts>
    hybrid_reader_tp<Ts...>::~hybrid_reader_tp() = default;
    template<typename... Ts>
    hybrid_reader_tp<Ts...>::hybrid_reader_tp(const hybrid_reader_tp&) = default;
    template<typename... Ts>
    hybrid_reader_tp<Ts...>::hybrid_reader_tp(hybrid_reader_tp&) = default;
    template<typename... Ts>
    hybrid_reader_tp<Ts...>::hybrid_reader_tp(hybrid_reader_tp&&) = default;

    template<typename... Ts>
    hybrid_reader_tp<Ts...>& hybrid_reader_tp<Ts...>::operator=(
        const hybrid_reader_tp&) = default;
    template<typename... Ts>
    hybrid_reader_tp<Ts...>& hybrid_reader_tp<Ts...>::operator=(
        hybrid_reader_tp&) = default;
    template<typename... Ts>
    hybrid_reader_tp<Ts...>& hybrid_reader_tp<Ts...>::operator=(
        hybrid_reader_tp&&) = default;

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
    bool hybrid_reader_tp<Ts...>::read(sample& s, std::error_code& ec) const
    {
        return std::apply(caller{ s, ec }, _readers);
    }

    template<typename... Ts>
    bool hybrid_reader_tp<Ts...>::read(sample&, uint8_t, std::error_code& ec) const
    {
        ec = errc::operation_not_supported;
        return false;
    }

    template<typename... Ts>
    size_t hybrid_reader_tp<Ts...>::num_events() const noexcept
    {
        return std::apply(
            [](const Ts&... args)
            {
                return (args.num_events() + ...);
            }, _readers);
    }
}
