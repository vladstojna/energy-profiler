#pragma once

#include <type_traits>

namespace nrgprf
{
    class reader;

    namespace detail
    {
        template<typename... Ts>
        using all_reader_ptrs = typename std::enable_if<
            std::conjunction<std::is_convertible<Ts*, reader*>...>::value, bool>::type;
    }
}
