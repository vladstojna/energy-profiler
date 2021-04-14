// expected.hpp

#pragma once

#include <variant>

#if defined(DEBUG_MODE)
#include <iostream>
#define DBG(x) do { x; } while (false)
#else
#define DBG(x) do { } while (false)
#endif

namespace cmmn
{

    template<typename R, typename E>
    class expected
    {
        static_assert(!std::is_void_v<R>, "Result cannot be void");
        static_assert(!std::is_reference_v<R>, "Result must be a value type");
        static_assert(!std::is_void_v<E>, "Result cannot be void");
        static_assert(!std::is_reference_v<E>, "Result must be a value type");
        static_assert(!std::is_same_v<R, E>, "Result and Error must be different types");

    private:
        std::variant<E, R> _result;

    public:

        template<typename T = E, std::enable_if_t<std::is_scalar_v<T>, bool> = true>
        expected(E error) :
            _result(error)
        {
            DBG(std::cout << "[result@"
                << reinterpret_cast<uintptr_t>(this)
                << ": value error]\n");
        }

        template<typename T = E, std::enable_if_t<!std::is_scalar_v<T>, bool> = true>
        expected(const E& error) :
            _result(error)
        {
            DBG(std::cout << "[result@"
                << reinterpret_cast<uintptr_t>(this)
                << ": const& error]\n");
        }

        template<typename T = E, std::enable_if_t<!std::is_scalar_v<T>, bool> = true>
        expected(E& error) :
            _result(error)
        {
            DBG(std::cout << "[result@"
                << reinterpret_cast<uintptr_t>(this)
                << ": const& error]\n");
        }

        template<typename T = E, std::enable_if_t<!std::is_scalar_v<T>, bool> = true>
        expected(E&& error) :
            _result(std::move(error))
        {
            DBG(std::cout << "[result@"
                << reinterpret_cast<uintptr_t>(this)
                << ": && error]\n");
        }

        template<typename T = R, std::enable_if_t<std::is_scalar_v<T>, bool> = true>
        expected(R result) :
            _result(result)
        {
            DBG(std::cout << "[result@"
                << reinterpret_cast<uintptr_t>(this)
                << ": value result]\n");
        }

        template<typename T = R, std::enable_if_t<!std::is_scalar_v<T>, bool> = true>
        expected(const R& result) :
            _result(result)
        {
            DBG(std::cout << "[result@"
                << reinterpret_cast<uintptr_t>(this)
                << ": const& result]\n");
        }

        template<typename T = R, std::enable_if_t<!std::is_scalar_v<T>, bool> = true>
        expected(R&& result) :
            _result(std::move(result))
        {
            DBG(std::cout << "[result@"
                << reinterpret_cast<uintptr_t>(this)
                << ": && result]\n");
        }

        template<typename... Args, typename T = R, std::enable_if_t<!std::is_scalar_v<T>, bool> = true>
        expected(Args&&... args) :
            _result(std::in_place_type<R>, std::forward<Args>(args)...)
        {
            DBG(std::cout << "[result@"
                << reinterpret_cast<uintptr_t>(this)
                << ": result constructed in place]\n");
        }

        template<typename T = E, std::enable_if_t<std::is_scalar_v<T>, bool> = true>
        E error() const
        {
            DBG(std::cout << "[error: returned by value]\n");
            return std::get<E>(_result);
        }

        template<typename T = E, std::enable_if_t<!std::is_scalar_v<T>, bool> = true>
        const E& error() const
        {
            DBG(std::cout << "[error: returned const&]\n");
            return std::get<E>(_result);
        }

        template<typename T = E, std::enable_if_t<!std::is_scalar_v<T>, bool> = true>
        E& error()
        {
            DBG(std::cout << "[error: returned &]\n");
            return std::get<E>(_result);
        }

        template<typename T = R, std::enable_if_t<std::is_scalar_v<T>, bool> = true>
        R value()
        {
            DBG(std::cout << "[result: returned by value]\n");
            return std::get<R>(_result);
        }

        template<typename T = R, std::enable_if_t<!std::is_scalar_v<T>, bool> = true>
        const R& value() const
        {
            DBG(std::cout << "[result: returned by const&]\n");
            return std::get<R>(_result);
        }

        template<typename T = R, std::enable_if_t<!std::is_scalar_v<T>, bool> = true>
        R& value()
        {
            DBG(std::cout << "[result: returned by &]\n");
            return std::get<R>(_result);
        }

        explicit operator bool() const
        {
            return !std::holds_alternative<E>(_result);
        }
    };

}
