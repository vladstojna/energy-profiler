#pragma once

#include <type_traits>

namespace nrgprf {
class reader;

namespace detail {
template <typename T>
using remove_cvref = std::remove_cv<std::remove_reference_t<T>>;

template <typename T> using remove_cvref_t = typename remove_cvref<T>::type;

template <typename... Ts>
using all_reader_ptrs = typename std::conjunction<
    std::is_convertible<remove_cvref_t<Ts> *, reader *>...>;

template <typename... Ts>
constexpr auto all_reader_ptrs_v = all_reader_ptrs<Ts...>::value;
} // namespace detail
} // namespace nrgprf
