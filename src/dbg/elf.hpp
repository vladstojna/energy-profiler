#pragma once

#include <cstdint>
#include <iosfwd>
#include <string>

namespace tep::dbg
{
    enum class executable_type
    {
        executable,
        shared_object,
    };

    enum class symbol_visibility : uint32_t
    {
        def,
        internal,
        hidden,
        prot,
    };

    enum class symbol_binding : uint32_t
    {
        local,
        global,
        weak,
    };

    struct executable_header
    {
        executable_type type;
        uintptr_t entrypoint_address;

        struct param;
        explicit executable_header(param);
    };

    struct function_symbol
    {
        std::string name;
        uintptr_t address;
        size_t size;
        symbol_visibility visibility;
        symbol_binding binding;

        uintptr_t global_entrypoint() const noexcept;
        uintptr_t local_entrypoint() const noexcept;

        struct param;
        explicit function_symbol(param);

    private:
        uint8_t st_other;
    };

    std::ostream& operator<<(std::ostream&, executable_type);
    std::ostream& operator<<(std::ostream&, symbol_visibility);
    std::ostream& operator<<(std::ostream&, symbol_binding);
    std::ostream& operator<<(std::ostream&, const executable_header&);
    std::ostream& operator<<(std::ostream&, const function_symbol&);
} // namespace tep::dbg
