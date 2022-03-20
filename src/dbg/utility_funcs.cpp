#include "utility_funcs.hpp"

#include <nonstd/expected.hpp>

#include <iostream>

namespace
{
    struct util_category_t : std::error_category
    {
        const char* name() const noexcept override
        {
            return "dbg-util";
        }

        std::string message(int ev) const override
        {
            using tep::dbg::util_errc;
            switch (static_cast<util_errc>(ev))
            {
            case util_errc::cu_not_found:
                return "Compilation unit not found";
            case util_errc::cu_ambiguous:
                return "Compilation unit ambiguous";
            case util_errc::file_not_found:
                return "File not found";
            case util_errc::line_not_found:
                return "Line not found";
            case util_errc::column_not_found:
                return "Column not found";
            }
            return "(unrecognized error code)";
        }
    };

    const util_category_t util_category_v;

    // check if sub is a subpath of path,
    // i.e. sub is an incomplete path of path
    bool is_sub_path(
        const std::filesystem::path& sub, const std::filesystem::path& path)
    {
        return !sub.empty() && (sub == path ||
            std::search(
                path.begin(), path.end(), sub.begin(), sub.end()) != path.end());
    }
}

namespace tep::dbg
{
    std::error_code make_error_code(util_errc x) noexcept
    {
        return { static_cast<int>(x), util_category() };
    }

    const std::error_category& util_category() noexcept
    {
        return util_category_v;
    }

    result<const compilation_unit*>
        find_compilation_unit(
            const object_info& oi,
            const std::filesystem::path& cu) noexcept
    {
        using unexpected = nonstd::unexpected<std::error_code>;
        auto find = [&cu](auto first, auto last)
        {
            return std::find_if(first, last,
                [&cu](const compilation_unit& x)
                {
                    return is_sub_path(cu, x.path);
                });
        };

        auto end_it = oi.compilation_units().end();
        auto it = find(oi.compilation_units().begin(), end_it);
        if (it == end_it)
            return unexpected{ util_errc::cu_not_found };
        // find again from returned iterator to end to see if CU is ambiguous
        if (find(it + 1, end_it) != end_it)
            return unexpected{ util_errc::cu_ambiguous };
        return &*it;
    }

    result<std::pair<lines::const_iterator, lines::const_iterator>>
        find_lines(
            const compilation_unit& cu,
            const std::filesystem::path& file,
            uint32_t lineno,
            exact_line_value_flag exact_line,
            uint32_t colno,
            exact_column_value_flag exact_col) noexcept
    {
        using unexpected = nonstd::unexpected<std::error_code>;
        if (!lineno && colno)
            return unexpected{ make_error_code(std::errc::invalid_argument) };

        const auto& effective_file = file.empty() ? cu.path : file;

        static auto line_match = [](
            const source_line& line, uint32_t lineno, exact_line_value_flag exact_line)
        {
            return !lineno || (exact_line == exact_line_value_flag::yes ?
                line.number == lineno :
                line.number >= lineno);
        };

        static auto column_match = [](
            const source_line& line, uint32_t colno, exact_column_value_flag exact_col)
        {
            return !colno || (exact_col == exact_column_value_flag::yes ?
                line.column == colno :
                line.column >= colno);
        };

        bool file_found = false;
        auto start_it = std::find_if(cu.lines.begin(), cu.lines.end(),
            [&effective_file, &file_found, lineno, exact_line](const source_line& line)
            {
                return (file_found = (effective_file == line.file)) &&
                    line_match(line, lineno, exact_line);
            });
        if (start_it == cu.lines.end())
        {
            if (!file_found)
                return unexpected{ util_errc::file_not_found };
            return unexpected{ util_errc::line_not_found };
        }

        start_it = std::find_if(start_it, cu.lines.end(),
            [&effective_file, lineno = start_it->number, colno, exact_col](
                const source_line& line)
        {
            return effective_file == line.file &&
                line_match(line, lineno, exact_line_value_flag::yes) &&
                column_match(line, colno, exact_col);
        });
        if (start_it == cu.lines.end())
            return unexpected{ util_errc::column_not_found };

        auto end_it = std::find_if_not(start_it, cu.lines.end(),
            [&effective_file, lineno = start_it->number](const source_line& line)
        {
            return effective_file == line.file &&
                line_match(line, lineno, exact_line_value_flag::yes);
        });

        end_it = std::find_if_not(end_it, cu.lines.end(),
            [&effective_file, lineno = start_it->number, colno = start_it->column](
                const source_line& line)
        {
            return effective_file == line.file &&
                line_match(line, lineno, exact_line_value_flag::yes) &&
                column_match(line, colno, exact_column_value_flag::yes);
        });
        assert(std::distance(start_it, end_it) > 0);
        return std::pair{ start_it, end_it };
    }

    result<uintptr_t> lowest_line_address(
        lines::const_iterator first,
        lines::const_iterator last,
        new_statement_flag new_stmt) noexcept
    {
        using unexpected = nonstd::unexpected<std::error_code>;
        if (new_stmt == new_statement_flag::no)
            return first->address;
        auto it = std::find_if(first, last, [](const source_line& line)
            {
                return line.new_statement;
            });
        if (it == last)
            return unexpected{ util_errc::line_not_found };
        return it->address;
    }

    result<uintptr_t> highest_line_address(
        lines::const_iterator first,
        lines::const_iterator last,
        new_statement_flag new_stmt) noexcept
    {
        using unexpected = nonstd::unexpected<std::error_code>;
        if (new_stmt == new_statement_flag::no)
            return (first + std::distance(first, last) - 1)->address;
        auto found = last;
        for (auto it = first; it != last; ++it)
            if (it->new_statement)
                found = it;
        if (found == last)
            return unexpected{ util_errc::line_not_found };
        return found->address;
    }
} // namespace tep::dbg
