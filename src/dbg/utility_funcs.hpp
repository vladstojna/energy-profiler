#pragma once

#include "object_info.hpp"

#include <util/expectedfwd.hpp>

#include <filesystem>
#include <system_error>

namespace tep::dbg {
template <typename T> using result = nonstd::expected<T, std::error_code>;

using lines = compilation_unit::container<source_line>;
using functions = compilation_unit::container<function>;

enum class util_errc : uint32_t {
  cu_not_found = 1,
  cu_ambiguous,
  file_not_found,
  line_not_found,
  column_not_found,
  symbol_not_found,
  symbol_ambiguous,
  symbol_ambiguous_static,
  symbol_ambiguous_weak,
  symbol_ambiguous_suffix,
  no_matches,
  function_not_found,
  function_ambiguous,
  decl_location_not_found,
  address_not_found,
};

enum class util_errcause : uint32_t {
  not_found = 1,
  ambiguous,
  other,
};

enum class new_statement_flag : bool { no, yes };
enum class exact_line_value_flag : bool { no, yes };
enum class exact_column_value_flag : bool { no, yes };
enum class exact_symbol_name_flag : bool { no, yes };
enum class ignore_symbol_suffix_flag : bool { no, yes };
} // namespace tep::dbg

namespace std {
template <> struct is_error_code_enum<tep::dbg::util_errc> : std::true_type {};
template <>
struct is_error_condition_enum<tep::dbg::util_errcause> : std::true_type {};
} // namespace std

namespace tep::dbg {
std::error_code make_error_code(util_errc) noexcept;
std::error_condition make_error_condition(util_errcause) noexcept;
const std::error_category &util_category() noexcept;

/**
 * @brief Finds compilation unit in object info if path to find
 * equals or is a subpath of the CU path.
 *
 * @param cu compilation unit to find
 * @return result<const compilation_unit*>
 */
result<const compilation_unit *>
find_compilation_unit(const object_info &,
                      const std::filesystem::path &cu) noexcept;

/**
 * @brief Find compilation unit in object info by address,
 * i.e., check whether an address belongs to the range of valid addresses
 * of a CU
 *
 * @param addr the address
 * @return result<const compilation_unit*>
 */
result<const compilation_unit *> find_compilation_unit(const object_info &,
                                                       uintptr_t addr) noexcept;

/**
 * @brief Find compilation unit from symbol
 *
 * @param sym the symbol that potentially belongs to a CU
 * @return result<const compilation_unit*>
 */
result<const compilation_unit *>
find_compilation_unit(const object_info &, const function_symbol &sym) noexcept;

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
result<std::pair<lines::const_iterator, lines::const_iterator>> find_lines(
    const compilation_unit &cu,
    const std::filesystem::path &file = std::filesystem::path{},
    uint32_t lineno = 0,
    exact_line_value_flag exact_line = exact_line_value_flag::yes,
    uint32_t colno = 0,
    exact_column_value_flag exact_col = exact_column_value_flag::yes) noexcept;

/**
 * @brief Use a declaration or call source location to find the corresponding
 * source line from .debug_line
 *
 * @param cu compilation unit to search
 * @param loc source location
 * @return result<const source_line*>
 */
result<const source_line *> find_line(const compilation_unit &cu,
                                      const source_location &loc) noexcept;

/**
 * @brief Find line with the lowest address in range according to constraints
 *
 * @param first the first line in the range
 * @param last the last line in the range
 * @param new_stmt whether the found line must be the start of a new statement
 * @return result<const source_line*>
 */
result<const source_line *> lowest_address_line(
    lines::const_iterator first, lines::const_iterator last,
    new_statement_flag new_stmt = new_statement_flag::no) noexcept;

/**
 * @brief Find line with the highest address in range according to constraints
 *
 * @param first the first line in the range
 * @param last the last line in the range
 * @param new_stmt whether the found line must be the start of a new statement
 * @return result<const source_line*>
 */
result<const source_line *> highest_address_line(
    lines::const_iterator first, lines::const_iterator last,
    new_statement_flag new_stmt = new_statement_flag::no) noexcept;

/**
 * @brief Find a function symbol from the loaded symbol table by name.
 * This is a global search.
 *
 * @param name demangled function symbol name
 * @param exact_name whether to match name exactly or as just
 * a prefix to the actual full name
 * @param no_suffix whether to prioritise symbol without suffix in case of
 * ambiguity
 * @return result<const function_symbol*>
 */
result<const function_symbol *> find_function_symbol(
    const object_info &, std::string_view name,
    exact_symbol_name_flag exact_name = exact_symbol_name_flag::no,
    ignore_symbol_suffix_flag no_suffix = ignore_symbol_suffix_flag::yes);

/**
 * @brief Find a function symbol defined in some compilation unit by name.
 *
 * @param cu compilation unit
 * @param name demangled function symbol name
 * @param exact_name whether to match name exactly or as just
 * a prefix to the actual full name
 * @param no_suffix whether to prioritise symbol without suffix in case of
 * ambiguity
 * @return result<const function_symbol*>
 */
result<const function_symbol *> find_function_symbol(
    const object_info &, const compilation_unit &cu, std::string_view name,
    exact_symbol_name_flag exact_name = exact_symbol_name_flag::no,
    ignore_symbol_suffix_flag no_suffix = ignore_symbol_suffix_flag::yes);

/**
 * @brief Find function symbol by address
 *
 * @param addr symbol address
 * @return result<const function_symbol*>
 */
result<const function_symbol *> find_function_symbol(const object_info &,
                                                     uintptr_t addr) noexcept;

/**
 * @brief Find ELF function symbol from function DWARF data
 *
 * @param f function to match with symbol
 * @return result<const function_symbol*>
 */
result<const function_symbol *>
find_function_symbol(const object_info &, const function &f) noexcept;

/**
 * @brief Find function (DWARF data) in compilation unit from ELF symbol
 *
 * @param cu the compilation unit to search
 * @param f the function symbol to match
 * @return result<const function*>
 */
result<const function *> find_function(const compilation_unit &cu,
                                       const function_symbol &f) noexcept;

/**
 * @brief Find function (DWARF data) from ELF symbol
 *
 * @param f the function symbol to match
 * @return result<const function*>
 */
result<const function *> find_function(const object_info &,
                                       const function_symbol &f) noexcept;

/**
 * @brief Find function (DWARF data) by name.
 * Searches the symbol table first followed by the debug information
 *
 * @param name demangled function name
 * @param exact_name whether to match name exactly or as just
 * a prefix to the actual full name
 * @return result<std::pair<const function*, const function_symbol*>>
 * the function symbol may be a nullptr if not found
 */
result<std::pair<const function *, const function_symbol *>>
find_function(const object_info &, std::string_view name,
              exact_symbol_name_flag exact_name = exact_symbol_name_flag::no);

/**
 * @brief Find function (DWARF data) in a compilation unit.
 * Used mostly to lookup static functions with equal names but
 * defined in different compilation units.
 *
 * @param cu the function's compilation unit
 * @param name demangled function name
 * @param exact_name whether to match name exactly or as just
 * a prefix to the actual full name
 * @return result<std::pair<const function*, const function_symbol*>>
 * the function symbol may be a nullptr if not found
 */
result<std::pair<const function *, const function_symbol *>>
find_function(const object_info &, const compilation_unit &cu,
              std::string_view name,
              exact_symbol_name_flag exact_name = exact_symbol_name_flag::no);

/**
 * @brief Find function (DWARF data) in a compilation unit without looking
 * up symbol first. The behaviour is as if the symbol had not been found.
 *
 * @param cu the function's compilation unit
 * @param name demangled function name
 * @param exact_name whether to match name exactly or as just
 * a prefix to the actual full name
 * @return result<const function*>
 */
result<const function *>
find_function(const compilation_unit &cu, std::string_view name,
              exact_symbol_name_flag exact_name = exact_symbol_name_flag::no);

/**
 * @brief Find all functions in a file from a compilation unit
 *
 * @param cu compilation unit to search
 * @param file file containing functions
 * @return result<std::pair<functions::const_iterator,
 * functions::const_iterator>>
 */
result<std::pair<functions::const_iterator, functions::const_iterator>>
find_functions(const compilation_unit &cu,
               const std::filesystem::path &file) noexcept;

/**
 * @brief Find function in compilation unit from source
 *
 * @param cu compilation unit to search
 * @param file function declaration file
 * @param lineno function declaration line
 * @param colno function declaration column or 0 to match any column
 * @return result<const function*>
 */
result<const function *> find_function(const compilation_unit &cu,
                                       const std::filesystem::path &file,
                                       uint32_t lineno,
                                       uint32_t colno = 0) noexcept;
} // namespace tep::dbg
