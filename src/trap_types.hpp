#pragma once

#include "dbg/fwd.hpp"
#include "output/fwd.hpp"

#include <iosfwd>
#include <string>

namespace tep
{
    struct address
    {
        uintptr_t value;
        const dbg::compilation_unit* cu;

        bool is_function_call() const noexcept;
    };

    struct function_call : address
    {
        const dbg::function* func;
        const dbg::function_symbol* sym;

        bool is_function_call() const noexcept;
    };

    struct function_return : address {};

    struct inline_function : address
    {
        const dbg::function* func;
        const dbg::function_symbol* sym;
        const dbg::inline_instance* inst;
    };

    struct source_line : address
    {
        const dbg::source_line* line;
    };

    std::string to_string(const address&);
    std::string to_string(const function_call&);
    std::string to_string(const function_return&);
    std::string to_string(const inline_function&);
    std::string to_string(const source_line&);

    std::ostream& operator<<(std::ostream&, const address&);
    std::ostream& operator<<(std::ostream&, const function_call&);
    std::ostream& operator<<(std::ostream&, const function_return&);
    std::ostream& operator<<(std::ostream&, const inline_function&);
    std::ostream& operator<<(std::ostream&, const source_line&);

    output_writer& operator<<(output_writer&, const address&);
    output_writer& operator<<(output_writer&, const function_call&);
    output_writer& operator<<(output_writer&, const function_return&);
    output_writer& operator<<(output_writer&, const inline_function&);
    output_writer& operator<<(output_writer&, const source_line&);
} // namespace tep
