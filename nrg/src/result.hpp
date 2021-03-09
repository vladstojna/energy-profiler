// result.hpp

#pragma once

#include <variant>

#if defined(DEBUG_MODE)
#include <iostream>
#define DBG(x) do { x; } while (false)
#else
#define DBG(x) do { } while (false)
#endif

#include "error.hpp"

namespace nrgprf
{

    template<typename R, typename E = error>
    class result
    {
        static_assert(!std::is_void_v<R>, "Result cannot be void");
        static_assert(!std::is_reference_v<R>, "Result must be a value type");

    private:
        std::variant<E, R> _result;

    public:
        result(const E& error) :
            _result(error)
        {
            DBG(std::cout << "[result: constructed error with cpy ctor]\n");
        }

        result(E&& error) :
            _result(std::move(error))
        {
            DBG(std::cout << "[result: constructed error with mv ctor]\n");
        }

        result(const R& result) :
            _result(result)
        {
            DBG(std::cout << "[result: constructed with cpy ctor]\n");
        }

        result(R&& result) :
            _result(std::move(result))
        {
            DBG(std::cout << "[result: constructed with mv ctor]\n");
        }

        template<typename... Args>
        result(Args&&... args) :
            _result(std::in_place_type<R>, args...)
        {
            DBG(std::cout << "[result: constructed in place]\n");
        }

        const E& error() const
        {
            return std::get<E>(_result);
        }

        template<typename T = R, std::enable_if_t<std::is_scalar<T>::value, bool> = true>
        T value() const
        {
            DBG(std::cout << "[result: returned by value]\n");
            return std::get<T>(_result);
        }

        template<typename T = R, std::enable_if_t<!std::is_scalar<T>::value, bool> = true>
        const T& value() const
        {
            DBG(std::cout << "[result: returned by const&]\n");
            return std::get<T>(_result);
        }

        template<typename T = R, std::enable_if_t<!std::is_scalar<T>::value, bool> = true>
        T& value()
        {
            DBG(std::cout << "[result: returned by &]\n");
            return std::get<T>(_result);
        }

        operator bool() const
        {
            return !std::holds_alternative<E>(_result);
        }
    };

}
