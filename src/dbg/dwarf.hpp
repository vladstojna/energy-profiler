#pragma once

#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace tep::dbg
{
    enum class line_context : uint32_t
    {
        prologue_end,
        none,
        epilogue_begin,
    };

    struct contiguous_range
    {
        uintptr_t low_pc;
        uintptr_t high_pc;
    };

    struct function_addresses
    {
        uintptr_t entry_pc = 0;
        contiguous_range crange = {};

        struct param;
        explicit function_addresses(const param&);
    };

    struct source_line
    {
        std::filesystem::path file;
        uint32_t number;
        uint32_t column;
        uintptr_t address;
        bool new_statement;
        bool new_basic_block;
        bool end_text_sequence;
        line_context ctx;

        struct param;
        explicit source_line(const param&);
    };

    struct source_location
    {
        std::filesystem::path file;
        uint32_t line_number = 0;
        uint32_t line_column = 0;

        struct decl_param;
        explicit source_location(decl_param);
        struct call_param;
        explicit source_location(call_param);
    };

    struct inline_instance
    {
        using ranges = std::vector<contiguous_range>;

        uintptr_t entry_pc = 0;
        std::optional<source_location> call_loc;
        std::variant<contiguous_range, ranges> addresses;

        struct param;
        explicit inline_instance(const param&);
    };

    struct function_base
    {
        std::string die_name;
        std::optional<source_location> decl_loc;

        struct param;
        explicit function_base(const param&);
    };

    using inline_instances = std::vector<inline_instance>;
    using ranges = std::vector<contiguous_range>;

    struct static_function : function_base
    {
        using data_t = std::variant<function_addresses, ranges, inline_instances>;

        data_t data;

        struct param;
        explicit static_function(const param&);
    };

    struct normal_function final : static_function
    {
        std::string linkage_name;

        struct param;
        explicit normal_function(const param&);
    };

    using any_function = std::variant<normal_function, static_function>;

    struct compilation_unit
    {
        template<typename T>
        using container = std::vector<T>;

        std::filesystem::path path;
        container<source_line> lines;
        container<any_function> funcs;

        struct param;
        explicit compilation_unit(const param&);

    private:
        void load_lines(const param&);
        void load_functions(const param&);
    };

    std::ostream& operator<<(std::ostream&, line_context);
    std::ostream& operator<<(std::ostream&, const contiguous_range&);
    std::ostream& operator<<(std::ostream&, const function_addresses&);
    std::ostream& operator<<(std::ostream&, const source_line&);
    std::ostream& operator<<(std::ostream&, const source_location&);
    std::ostream& operator<<(std::ostream&, const inline_instance&);
    std::ostream& operator<<(std::ostream&, const function_base&);
    std::ostream& operator<<(std::ostream&, const static_function&);
    std::ostream& operator<<(std::ostream&, const normal_function&);
    std::ostream& operator<<(std::ostream&, const any_function&);
    std::ostream& operator<<(std::ostream&, const compilation_unit&);
} // namespace tep::dbg
