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

    private:
        std::variant<E, R> _result;

    public:
        expected(const E& error) :
            _result(error)
        {
            DBG(std::cout << "[result@"
                << reinterpret_cast<uintptr_t>(this)
                << ": const& error]\n");
        }

        expected(E&& error) :
            _result(std::move(error))
        {
            DBG(std::cout << "[result@"
                << reinterpret_cast<uintptr_t>(this)
                << ": && error]\n");
        }

        expected(const R& result) :
            _result(result)
        {
            DBG(std::cout << "[result@"
                << reinterpret_cast<uintptr_t>(this)
                << ": const& result]\n");
        }

        expected(R&& result) :
            _result(std::move(result))
        {
            DBG(std::cout << "[result@"
                << reinterpret_cast<uintptr_t>(this)
                << ": && result]\n");
        }

        template<typename... Args>
        expected(Args&&... args) :
            _result(std::in_place_type<R>, std::forward<Args>(args)...)
        {
            DBG(std::cout << "[result@"
                << reinterpret_cast<uintptr_t>(this)
                << ": result constructed in place]\n");
        }

        const E& error() const
        {
            return std::get<E>(_result);
        }

        const R& value() const
        {
            DBG(std::cout << "[result: returned by const&]\n");
            return std::get<R>(_result);
        }

        R& value()
        {
            DBG(std::cout << "[result: returned by &]\n");
            return std::get<R>(_result);
        }

        operator bool() const
        {
            return !std::holds_alternative<E>(_result);
        }
    };

}
