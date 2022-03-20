#include "utility_funcs.hpp"
#include "demangle.hpp"

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
            case util_errc::symbol_not_found:
                return "Symbol not found";
            case util_errc::symbol_ambiguous:
                return "Symbol name ambiguous";
            case util_errc::symbol_ambiguous_static:
                return "Symbol name ambiguous; two or more static functions found";
            case util_errc::function_not_found:
                return "Function not found";
            case util_errc::function_ambiguous:
                return "Function ambiguous";
            case util_errc::decl_location_not_found:
                return "No function with declaration location found";
            }
            return "(unrecognized error code)";
        }
    };

    const util_category_t util_category_v;

    struct mangled_name_t {};
    struct demangled_name_t {};

    constexpr mangled_name_t mangled_name;
    constexpr demangled_name_t demangled_name;

    // check if sub is a subpath of path,
    // i.e. sub is an incomplete path of path
    bool is_sub_path(
        const std::filesystem::path& sub, const std::filesystem::path& path)
    {
        return !sub.empty() && (sub == path ||
            std::search(
                path.begin(), path.end(), sub.begin(), sub.end()) != path.end());
    }

    std::string remove_spaces(std::string_view str)
    {
        std::string ret(str);
        ret.erase(std::remove_if(ret.begin(), ret.end(), [](unsigned char c)
            {
                return std::isspace(c);
            }), ret.end());
        return ret;
    }

    tep::dbg::result<const tep::dbg::function_symbol*>
        find_function_symbol_exact(
            const tep::dbg::object_info& oi,
            std::string_view name)
    {
        using tep::dbg::function_symbol;
        using tep::dbg::util_errc;
        using unexpected = nonstd::unexpected<std::error_code>;

        std::error_code ec;
        auto pred = [&ec, name](const function_symbol& sym)
        {
            auto demangled = tep::dbg::demangle(sym.name, ec);
            if (!demangled)
                return true;
            return remove_spaces(*demangled) == remove_spaces(name);
        };

        auto end_it = oi.function_symbols().end();
        auto it = std::find_if(oi.function_symbols().begin(), end_it, pred);
        if (ec)
            return unexpected{ ec };
        if (it == end_it)
            return unexpected{ util_errc::symbol_not_found };
        auto second_it = std::find_if(it + 1, end_it, pred);
        if (second_it != end_it)
        {
            using tep::dbg::symbol_binding;
            if (it->binding == symbol_binding::local && it->binding == second_it->binding)
                return unexpected{ util_errc::symbol_ambiguous_static };
            return unexpected{ util_errc::symbol_ambiguous };
        }
        return &*it;
    }

    tep::dbg::result<const tep::dbg::compilation_unit::any_function*>
        find_function_by_linkage_name(
            mangled_name_t,
            const tep::dbg::compilation_unit& cu,
            std::string_view name) noexcept
    {
        using tep::dbg::compilation_unit;
        using tep::dbg::normal_function;
        using tep::dbg::util_errc;
        using unexpected = nonstd::unexpected<std::error_code>;
        auto pred = [name](const compilation_unit::any_function& af)
        {
            if (!std::holds_alternative<normal_function>(af))
                return false;
            const auto& f = std::get<normal_function>(af);
            return f.linkage_name == name;
        };
        auto it = std::find_if(cu.funcs.begin(), cu.funcs.end(), pred);
        if (it == cu.funcs.end())
            return unexpected{ util_errc::function_not_found };
        return &*it;
    }

    tep::dbg::result<const tep::dbg::compilation_unit::any_function*>
        find_function_by_linkage_name(
            demangled_name_t,
            const tep::dbg::compilation_unit& cu,
            std::string_view name)
    {
        using tep::dbg::compilation_unit;
        using tep::dbg::normal_function;
        using tep::dbg::util_errc;
        using unexpected = nonstd::unexpected<std::error_code>;
        std::error_code ec;
        auto pred = [&ec, name](const compilation_unit::any_function& af)
        {
            if (!std::holds_alternative<normal_function>(af))
                return false;
            const auto& f = std::get<normal_function>(af);
            auto demangled = tep::dbg::demangle(f.linkage_name, ec);
            if (!demangled)
                return true;
            return remove_spaces(*demangled) == remove_spaces(name);
        };

        auto it = std::find_if(cu.funcs.begin(), cu.funcs.end(), pred);
        if (ec)
            return unexpected{ ec };
        if (it == cu.funcs.end())
            return unexpected{ util_errc::function_not_found };
        return &*it;
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

    result<const function_symbol*> find_function_symbol(
        const object_info& oi,
        std::string_view name,
        exact_symbol_name_flag exact_name)
    {
        using unexpected = nonstd::unexpected<std::error_code>;
        if (name.empty())
            return unexpected{ make_error_code(std::errc::invalid_argument) };
        if (exact_name == exact_symbol_name_flag::no)
            return unexpected{ make_error_code(std::errc::invalid_argument) };
        return find_function_symbol_exact(oi, name);
    }

    result<const compilation_unit::any_function*>
        find_function(
            const compilation_unit& cu,
            const function_symbol& f)
        noexcept
    {
        using unexpected = nonstd::unexpected<std::error_code>;
        // static function, lookup using address
        // otherwise, lookup using linkage name
        if (f.binding == symbol_binding::local)
        {
            auto it = std::find_if(cu.funcs.begin(), cu.funcs.end(),
                [sym_addr = f.address](const compilation_unit::any_function& af)
            {
                if (!std::holds_alternative<static_function>(af))
                    return false;
                const auto& f = std::get<static_function>(af);
                if (!std::holds_alternative<function_addresses>(f.data))
                    return false;
                const auto& addrs = std::get<function_addresses>(f.data);
                assert(addrs.entry_pc == addrs.crange.low_pc);
                return addrs.entry_pc == sym_addr;
            });
            if (it == cu.funcs.end())
                return unexpected{ util_errc::function_not_found };
            return &*it;
        }
        return find_function_by_linkage_name(mangled_name, cu, f.name);
    }

    result<const compilation_unit::any_function*>
        find_function(
            const object_info& oi,
            const function_symbol& f
        ) noexcept
    {
        using unexpected = nonstd::unexpected<std::error_code>;
        for (const auto& cu : oi.compilation_units())
        {
            auto func = find_function(cu, f);
            if (func || func.error() != util_errc::function_not_found)
                return func;
        }
        return unexpected{ util_errc::function_not_found };
    }

    result<const compilation_unit::any_function*>
        find_function(
            const object_info& oi,
            std::string_view name,
            exact_symbol_name_flag exact_name)
    {
        using unexpected = nonstd::unexpected<std::error_code>;
        if (name.empty())
            return unexpected{ make_error_code(std::errc::invalid_argument) };
        if (exact_name == exact_symbol_name_flag::no)
            return unexpected{ make_error_code(std::errc::invalid_argument) };
        auto sym = find_function_symbol_exact(oi, name);
        if (sym)
            return find_function(oi, **sym);
        else if (sym.error() == util_errc::symbol_not_found)
        {
            for (const auto& cu : oi.compilation_units())
            {
                auto func = find_function_by_linkage_name(demangled_name, cu, name);
                if (func || func.error() != util_errc::function_not_found)
                    return func;
            }
            return unexpected{ util_errc::function_not_found };
        }
        return unexpected{ sym.error() };
    }

    result<const compilation_unit::any_function*>
        find_function(
            const compilation_unit& cu,
            const std::filesystem::path& file,
            uint32_t lineno,
            uint32_t colno)
        noexcept
    {
        using unexpected = nonstd::unexpected<std::error_code>;
        bool file_found{}, line_found{}, col_found{}, decl_loc_found{};
        auto pred = [&](const compilation_unit::any_function& af)
        {
            const auto& f = std::holds_alternative<normal_function>(af) ?
                std::get<normal_function>(af) :
                std::get<static_function>(af);
            return f.decl_loc && (decl_loc_found = true) &&
                f.decl_loc->file == file && (file_found = true) &&
                f.decl_loc->line_number == lineno && (line_found = true) &&
                (!colno || (f.decl_loc->line_column == colno && (col_found = true)));
        };

        auto it = std::find_if(cu.funcs.begin(), cu.funcs.end(), pred);
        if (it == cu.funcs.end())
        {
            auto ec = util_errc::function_not_found;
            if (!decl_loc_found)
                ec = util_errc::decl_location_not_found;
            else if (!file_found)
                ec = util_errc::file_not_found;
            else if (!line_found)
                ec = util_errc::line_not_found;
            else if (!col_found)
                ec = util_errc::column_not_found;
            return unexpected{ ec };
        }

        if (std::find_if(it + 1, cu.funcs.end(), pred) != cu.funcs.end())
            return unexpected{ util_errc::function_ambiguous };
        return &*it;
    }
} // namespace tep::dbg
