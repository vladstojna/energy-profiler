#pragma once

#include "object_info.hpp"

#include <util/expectedfwd.hpp>

#include <filesystem>
#include <system_error>

namespace tep::dbg
{
    template<typename T>
    using result = nonstd::expected<T, std::error_code>;

    using lines = compilation_unit::container<source_line>;
    using functions = compilation_unit::container<compilation_unit::any_function>;

    enum class util_errc : uint32_t
    {
        cu_not_found = 1,
        cu_ambiguous,
        line_not_found,
        column_not_found,
    };

    enum class new_statement_flag : bool { no, yes };
    enum class exact_line_value_flag : bool { no, yes };
    enum class exact_column_value_flag : bool { no, yes };
} // namespace tep::dbg

namespace std
{
    template<> struct is_error_code_enum<tep::dbg::util_errc> : std::true_type {};
}

namespace tep::dbg
{
    std::error_code make_error_code(util_errc) noexcept;
    const std::error_category& util_category() noexcept;

    /**
     * @brief Finds compilation unit in object info if path to find
     * equals or is a subpath of the CU path.
     *
     * @param cu compilation unit to find
     * @return result<const compilation_unit*>
     */
    result<const compilation_unit*>
        find_compilation_unit(
            const object_info&, const std::filesystem::path& cu) noexcept;

    /**
     * @brief Finds an interval of compatible lines according to constraints
     * passed as arguments
     *
     * @param file the file the lines belong to or the CU's file if empty
     * @param lineno the line number to find or 0 to match any line number
     * @param exact_line whether to find the exact line number or
     * the first that is greater than or equal to it
     * @param colno the column number to find or 0 to match any line number
     * @param exact_col whether to find the exact column number or
     * the first that is greater than or equal to it
     * @return result<std::pair<lines::const_iterator, lines::const_iterator>>
     */
    result<std::pair<lines::const_iterator, lines::const_iterator>>
        find_lines(
            const compilation_unit& cu,
            const std::filesystem::path& file = std::filesystem::path{},
            uint32_t lineno = 0,
            exact_line_value_flag exact_line = exact_line_value_flag::yes,
            uint32_t colno = 0,
            exact_column_value_flag exact_col = exact_column_value_flag::yes)
        noexcept;

    /**
     * @brief Find the lowest address of line range according to constraints
     *
     * @param first the first line in the range
     * @param last the last line in the range
     * @param new_stmt whether the found line must be the start of a new statement
     * @return result<uintptr_t>
     */
    result<uintptr_t> lowest_line_address(
        lines::const_iterator first,
        lines::const_iterator last,
        new_statement_flag new_stmt = new_statement_flag::no) noexcept;

    /**
     * @brief Find the highest address of line range according to constraints
     *
     * @param first the first line in the range
     * @param last the last line in the range
     * @param new_stmt whether the found line must be the start of a new statement
     * @return result<uintptr_t>
     */
    result<uintptr_t> highest_line_address(
        lines::const_iterator first,
        lines::const_iterator last,
        new_statement_flag new_stmt = new_statement_flag::no) noexcept;
} // namespace tep::dbg
