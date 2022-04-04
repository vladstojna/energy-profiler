#pragma once

#include "output/fwd.hpp"

#include <iosfwd>
#include <memory>
#include <string>

namespace tep
{
    struct trap_context
    {
    public:
        template <typename T>
        explicit trap_context(T x);

        bool is_function_call() const noexcept;
        uintptr_t addr() const noexcept;

        friend std::string to_string(const trap_context&);
        friend std::ostream& operator<<(std::ostream&, const trap_context&);
        friend output_writer& operator<<(output_writer&, const trap_context&);

    private:
        template<typename, typename = void>
        struct has_trap_context_type_member
            : std::false_type
        {};

        template<typename T>
        struct has_trap_context_type_member<T, std::void_t<typename T::is_trap_context>>
            : std::true_type
        {};

        struct concept_t
        {
            virtual ~concept_t() = default;
            virtual uintptr_t addr() const noexcept = 0;
            virtual bool is_function_call() const noexcept = 0;
            virtual std::string as_string() const = 0;
            virtual void print(std::ostream&) const = 0;
            virtual void print(output_writer&) const = 0;
        };

        template <typename T>
        struct model_t : concept_t
        {
            static_assert(
                std::is_same_v<T, std::remove_reference_t<std::remove_cv_t<T>>>,
                "T must be a non-const, non-volatile value type");

            static_assert(
                has_trap_context_type_member<T>::value,
                "T must have a type member named is_trap_context");

            explicit model_t(T x)
                noexcept(std::is_nothrow_move_constructible_v<T>);

            uintptr_t addr() const noexcept override;
            bool is_function_call() const noexcept override;
            void print(std::ostream& os) const override;
            void print(output_writer& os) const override;
            std::string as_string() const override;

            T data_;
        };

        std::shared_ptr<const concept_t> self_;
    };

    std::string to_string(const trap_context&);
    std::ostream& operator<<(std::ostream&, const trap_context&);
    output_writer& operator<<(output_writer&, const trap_context&);
} // namespace tep

#include "trap_context.inl"
