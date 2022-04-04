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
    template<typename T>
    class passkey
    {
        friend T;
        explicit passkey() = default;
    };

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

    struct function_addresses
    {
        std::vector<contiguous_range> values;

        struct param;
        explicit function_addresses(const param&);
    };

    struct inline_instance
    {
        uintptr_t entry_pc = 0;
        std::optional<source_location> call_loc;
        function_addresses addresses;

        struct param;
        explicit inline_instance(const param&);
    };

    struct inline_instances
    {
        std::vector<inline_instance> insts;

        struct param;
        explicit inline_instances(const param&);

    private:
        std::vector<inline_instance> get_instances(const param&);
    };

    struct compilation_unit;

    struct function
    {
        std::string die_name;
        std::optional<source_location> decl_loc;
        std::optional<std::string> linkage_name;
        std::optional<function_addresses> addresses;
        std::optional<inline_instances> instances;

        struct param;
        explicit function(const param&);

        void set_out_of_line_addresses(
            function_addresses, passkey<compilation_unit>);
        void set_inline_instances(
            inline_instances, passkey<compilation_unit>);

        bool is_static() const noexcept;
        bool is_extern() const noexcept;
    };

    struct compilation_unit
    {
        template<typename T>
        using container = std::vector<T>;

        std::filesystem::path path;
        container<contiguous_range> addresses;
        container<source_line> lines;
        container<function> funcs;

        struct param;
        explicit compilation_unit(const param&);

    private:
        void load_lines(const param&);
        void load_functions(const param&);
    };

    bool operator==(const source_location&, const source_location&) noexcept;

    std::ostream& operator<<(std::ostream&, line_context);
    std::ostream& operator<<(std::ostream&, const contiguous_range&);
    std::ostream& operator<<(std::ostream&, const function_addresses&);
    std::ostream& operator<<(std::ostream&, const source_line&);
    std::ostream& operator<<(std::ostream&, const source_location&);
    std::ostream& operator<<(std::ostream&, const function_addresses&);
    std::ostream& operator<<(std::ostream&, const inline_instance&);
    std::ostream& operator<<(std::ostream&, const inline_instances&);
    std::ostream& operator<<(std::ostream&, const function&);
    std::ostream& operator<<(std::ostream&, const compilation_unit&);
} // namespace tep::dbg
