// config.hpp

#pragma once

#include <chrono>
#include <iosfwd>
#include <string>
#include <vector>
#include <optional>
#include <variant>
#include <memory>
#include <system_error>

namespace tep
{
    namespace cfg
    {
        enum class errc : uint32_t;
    }
}

namespace std
{
    template<> struct is_error_code_enum<tep::cfg::errc> : std::true_type {};
}

namespace tep
{
    namespace cfg
    {
        struct config_entry;

        enum class errc : uint32_t
        {
            config_io_error = 1,
            config_not_found,
            config_out_of_mem,
            config_bad_format,
            config_no_config,
            sec_no_bounds,
            sec_no_freq,
            sec_no_interval,
            sec_no_method,
            sec_invalid_target,
            sec_invalid_label,
            sec_invalid_extra,
            sec_invalid_freq,
            sec_invalid_interval,
            sec_invalid_method,
            sec_invalid_execs,
            sec_invalid_samples,
            sec_invalid_duration,
            sec_label_already_exists,
            sec_both_short_and_long,
            sec_invalid_method_for_short,
            group_empty,
            group_invalid_label,
            group_label_already_exists,
            group_invalid_extra,
            param_invalid_domain_mask,
            param_invalid_socket_mask,
            param_invalid_device_mask,
            bounds_no_start,
            bounds_no_end,
            bounds_empty,
            bounds_too_many,
            pos_no_comp_unit,
            pos_no_line,
            pos_invalid_comp_unit,
            pos_invalid_line,
            func_invalid_comp_unit,
            func_no_name,
            func_invalid_name,
            addr_range_no_start,
            addr_range_no_end,
            addr_range_invalid_value,
        };

        struct exception : std::system_error
        {
            using system_error::system_error;
        };

        std::error_code make_error_code(errc) noexcept;
        const std::error_category& config_category() noexcept;

        enum class target : uint32_t
        {
            cpu = 1 << 0,
            gpu = 1 << 1,
        };

        bool target_valid(target) noexcept;
        bool target_multiple(target) noexcept;
        target target_next(target) noexcept;

        target operator|(target, target) noexcept;
        target operator&(target, target) noexcept;
        target operator^(target, target) noexcept;
        target operator~(target) noexcept;
        target& operator|=(target&, target) noexcept;
        target& operator&=(target&, target) noexcept;
        target& operator^=(target&, target) noexcept;

        template<typename T>
        struct key
        {
            friend T;
        private:
            explicit key() = default;
        };

        struct section_t;

        struct params_t
        {
            std::optional<uint32_t> domain_mask;
            std::optional<uint32_t> socket_mask;
            std::optional<uint32_t> device_mask;

            explicit params_t(const config_entry&);
        };

        struct address_range_t
        {
            uint32_t start;
            uint32_t end;

            explicit address_range_t(const config_entry&);
        };

        struct position_t
        {
            std::string compilation_unit;
            uint32_t line;

            explicit position_t(const config_entry&);
        };

        struct function_t
        {
            std::optional<std::string> compilation_unit;
            std::string name;

            explicit function_t(const config_entry&);
        };

        class bounds_t
        {
        public:
            using position_range_t
                = std::pair<position_t, position_t>;

            template<typename T>
            bool holds() const noexcept
            {
                return std::holds_alternative<T>(_value);
            }

            template<typename T>
            const T& get() const
            {
                return std::get<T>(_value);
            }

            explicit bounds_t(const config_entry&, key<section_t>);

            friend std::ostream& operator<<(std::ostream&, const bounds_t&);
            friend bool operator==(const bounds_t&, const bounds_t&);

        private:
            using holder_type = std::variant<
                std::monostate,
                address_range_t,
                position_range_t,
                function_t
            >;
            holder_type _value;
        };

        struct method_total_t
        {
            bool short_section;

            explicit method_total_t(const config_entry&);
        };

        struct method_profile_t
        {
            std::chrono::milliseconds interval;
            std::optional<uint32_t> samples;

            explicit method_profile_t(const config_entry&);
        };

        struct misc_attributes_t
        {
            template<typename T>
            bool holds() const noexcept
            {
                return std::holds_alternative<T>(_value);
            }

            template<typename T>
            const T& get() const
            {
                return std::get<T>(_value);
            }

            explicit misc_attributes_t(const config_entry&, key<section_t>);

            friend std::ostream& operator<<(std::ostream&, const misc_attributes_t&);
            friend bool operator==(const misc_attributes_t&, const misc_attributes_t&);

        private:
            using holder_type = std::variant<
                std::monostate,
                method_total_t,
                method_profile_t
            >;
            holder_type _value;
        };

        struct section_t
        {
            std::optional<std::string> label;
            std::optional<std::string> extra;
            target targets;
            misc_attributes_t misc;
            bounds_t bounds;
            bool allow_concurrency;

            explicit section_t(const config_entry&);
        };

        struct group_t
        {
            std::optional<std::string> label;
            std::optional<std::string> extra;
            std::vector<section_t> sections;

            explicit group_t(const config_entry&);
        };

        struct config_t
        {
            using opt_params_t = std::optional<params_t>;
            using groups_t = std::vector<group_t>;

            const opt_params_t& parameters() const noexcept;
            const groups_t& groups() const noexcept;

            explicit config_t(std::istream&);

        private:
            struct impl;
            std::shared_ptr<const impl> _impl;
        };

        std::ostream& operator<<(std::ostream&, const target&);
        std::ostream& operator<<(std::ostream&, const params_t&);
        std::ostream& operator<<(std::ostream&, const address_range_t&);
        std::ostream& operator<<(std::ostream&, const function_t&);
        std::ostream& operator<<(std::ostream&, const position_t&);
        std::ostream& operator<<(std::ostream&, const bounds_t::position_range_t&);
        std::ostream& operator<<(std::ostream&, const bounds_t&);
        std::ostream& operator<<(std::ostream&, const method_total_t&);
        std::ostream& operator<<(std::ostream&, const method_profile_t&);
        std::ostream& operator<<(std::ostream&, const misc_attributes_t&);
        std::ostream& operator<<(std::ostream&, const section_t&);
        std::ostream& operator<<(std::ostream&, const group_t&);
        std::ostream& operator<<(std::ostream&, const config_t&);

        bool operator==(const params_t&, const params_t&);
        bool operator==(const address_range_t&, const address_range_t&);
        bool operator==(const position_t&, const position_t&);
        bool operator==(const function_t&, const function_t&);
        bool operator==(const bounds_t&, const bounds_t&);
        bool operator==(const misc_attributes_t&, const misc_attributes_t&);
        bool operator==(const section_t&, const section_t&);
        bool operator==(const group_t&, const group_t&);
    }
}
