#pragma once

#include <system_error>

namespace dbg
{
    enum class errc : uint32_t;
    enum class error_cause : uint32_t;
}

namespace std
{
    template<> struct is_error_code_enum<dbg::errc> : std::true_type {};
    template<> struct is_error_condition_enum<dbg::error_cause> : std::true_type {};
}

namespace dbg
{
    enum class errc : uint32_t
    {
        not_an_elf_object = 1,
        symtab_not_found,
        unsupported_object_type,
        invalid_symbol_visibility,
        unsupported_symbol_binding,
        line_number_overflow,
        line_column_overflow,
        no_linkage_name,
        no_low_pc_concrete,
        no_high_pc_concrete,
        no_low_pc_inlined,
        no_high_pc_inlined,
        unknown,
    };

    enum class error_cause : uint32_t
    {
        elf_error = 1,
        dwarf_error,
        custom_error,
        unknown,
    };

    struct exception : std::system_error
    {
    public:
        using system_error::system_error;
    };

    const std::error_category& generic_category() noexcept;
    const std::error_category& dwarf_category() noexcept;
    const std::error_category& elf_category() noexcept;

    std::error_code make_error_code(errc) noexcept;
    std::error_condition make_error_condition(error_cause) noexcept;
} // namespace dbg
