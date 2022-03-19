#include "error.hpp"

#include <elfutils/libdw.h>
#include <libelf.h>

namespace
{
    struct generic_category_t : std::error_category
    {
        const char* name() const noexcept override;
        std::string message(int) const override;
        std::error_condition default_error_condition(int) const noexcept override;
    };

    struct dwarf_category_t : std::error_category
    {
        const char* name() const noexcept override;
        std::string message(int) const override;
        std::error_condition default_error_condition(int) const noexcept override;
    };

    struct elf_category_t : std::error_category
    {
        const char* name() const noexcept override;
        std::string message(int) const override;
        std::error_condition default_error_condition(int) const noexcept override;
    };

    struct error_cause_category_t : std::error_category
    {
        const char* name() const noexcept override;
        std::string message(int) const override;
        bool equivalent(const std::error_code&, int) const noexcept override;
    };

    const generic_category_t generic_category_v;
    const dwarf_category_t dwarf_category_v;
    const elf_category_t elf_category_v;
    const error_cause_category_t error_cause_category_v;

    const char* generic_category_t::name() const noexcept
    {
        return "generic";
    }

    std::string generic_category_t::message(int ev) const
    {
        using tep::dbg::errc;
        switch (static_cast<errc>(ev))
        {
        case errc::not_an_elf_object:
            return "Not an ELF object";
        case errc::symtab_not_found:
            return "Symbol table not found";
        case errc::unsupported_object_type:
            return "Unsupported object type, must be DYN or EXEC";
        case errc::invalid_symbol_visibility:
            return "Function symbol with invalid visibility found";
        case errc::unsupported_symbol_binding:
            return "Unsupported function symbol binding; not local, global or weak";
        case errc::line_number_overflow:
            return "Line number value overflow";
        case errc::line_column_overflow:
            return "Line column value overflow";
        case errc::no_linkage_name:
            return "Line column value overflow";
        case errc::no_low_pc_concrete:
            return "No low PC in concrete function instance";
        case errc::no_high_pc_concrete:
            return "No high PC in concrete function instance";
        case errc::no_low_pc_inlined:
            return "No low PC in inlined function instance without multiple ranges";
        case errc::no_high_pc_inlined:
            return "No high PC in inlined function instance without multiple ranges";
        case errc::unknown:
            return "Unknown error";
        }
        return "(unrecognized error code)";
    }

    std::error_condition generic_category_t::default_error_condition(int ev) const noexcept
    {
        using tep::dbg::errc;
        using tep::dbg::error_cause;
        auto ec = static_cast<errc>(ev);
        return ec >= errc::not_an_elf_object && ec <= errc::unknown ?
            error_cause::custom_error :
            error_cause::unknown;
    }

    const char* dwarf_category_t::name() const noexcept
    {
        return "libdw";
    }

    std::string dwarf_category_t::message(int ev) const
    {
        return dwarf_errmsg(ev);
    }

    std::error_condition dwarf_category_t::default_error_condition(int) const noexcept
    {
        return tep::dbg::error_cause::dwarf_error;
    }

    const char* elf_category_t::name() const noexcept
    {
        return "libdw";
    }

    std::string elf_category_t::message(int ev) const
    {
        return elf_errmsg(ev);
    }

    std::error_condition elf_category_t::default_error_condition(int) const noexcept
    {
        return tep::dbg::error_cause::elf_error;
    }

    const char* error_cause_category_t::name() const noexcept
    {
        return "error-cause";
    }

    std::string error_cause_category_t::message(int ev) const
    {
        using tep::dbg::error_cause;
        switch (static_cast<error_cause>(ev))
        {
        case error_cause::elf_error:
            return "ELF library error";
        case error_cause::dwarf_error:
            return "DWARF library error";
        case error_cause::custom_error:
            return "Custom error";
        case error_cause::unknown:
            return "Unknown cause";
        }
        return "(unrecognized error cause)";
    }

    bool error_cause_category_t::equivalent(const std::error_code& ec, int cv) const noexcept
    {
        using tep::dbg::error_cause;
        auto cond = static_cast<error_cause>(cv);
        if (ec.category() == tep::dbg::generic_category())
            return cond == error_cause::custom_error;
        if (ec.category() == tep::dbg::elf_category())
            return cond == error_cause::elf_error;
        if (ec.category() == tep::dbg::dwarf_category())
            return cond == error_cause::dwarf_error;
        return false;
    }

}

namespace tep
{
    namespace dbg
    {
        const std::error_category& generic_category() noexcept
        {
            return generic_category_v;
        }

        const std::error_category& dwarf_category() noexcept
        {
            return dwarf_category_v;
        }

        const std::error_category& elf_category() noexcept
        {
            return elf_category_v;
        }

        std::error_code make_error_code(errc x) noexcept
        {
            return { static_cast<int>(x), generic_category() };
        }

        std::error_condition make_error_condition(error_cause x) noexcept
        {
            return { static_cast<int>(x), error_cause_category_v };
        }
    } // namespace dbg
} // namespace tep
