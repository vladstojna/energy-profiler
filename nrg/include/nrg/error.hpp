// error.hpp

#pragma once

#include <string>
#include <memory>

#include <system_error>

namespace nrgprf
{
    enum class errc : uint32_t;
    enum class error_cause : uint32_t;
}

namespace std
{
    template<> struct is_error_code_enum<nrgprf::errc> : std::true_type {};
    template<> struct is_error_condition_enum<nrgprf::error_cause> : std::true_type {};
}

namespace nrgprf
{
    enum class errc : uint32_t
    {
        not_implemented = 1,
        no_events_added,
        no_such_event,
        no_sockets_found,
        no_devices_found,
        too_many_sockets,
        too_many_devices,
        invalid_domain_name,
        file_format_version_error,
        operation_not_supported,
        energy_readings_not_supported,
        power_readings_not_supported,
        readings_not_supported,
        readings_not_valid,
        package_num_error,
        package_num_wrong_domain,
        unknown_error,
    };

    enum class error_cause : uint32_t
    {
        gpu_lib_error = 1,
        setup_error,
        query_error,
        read_error,
        system_error,
        readings_support_error,
        other,
        unknown,
    };

    struct exception : std::system_error
    {
        using system_error::system_error;
    };

    std::error_code make_error_code(errc) noexcept;
    std::error_condition make_error_condition(error_cause) noexcept;

    const std::error_category& generic_category() noexcept;
    const std::error_category& gpu_category() noexcept;

    // error codes

    enum class error_code
    {
        SUCCESS = 0,
        SYSTEM,
        NOT_IMPL,
        READ_ERROR,
        SETUP_ERROR,
        NO_EVENT,
        OUT_OF_BOUNDS,
        BAD_ALLOC,
        READER_GPU,
        READER_CPU,
        NO_SOCKETS,
        NO_DEVICES,
        TOO_MANY_SOCKETS,
        TOO_MANY_DEVICES,
        FORMAT_ERROR,
        INVALID_DOMAIN_NAME,
        UNSUPPORTED,
        UNKNOWN_ERROR,
    };

    // error holder

    class error
    {
        struct data
        {
            error_code code;
            std::string msg;

            data(error_code code);
            data(error_code code, const char* message);
            data(error_code code, const std::string& message);
            data(error_code code, std::string&& message);
        };

    public:
        static error success();

    private:
        std::unique_ptr<data> _data;

    public:
        error(error_code code);
        error(error_code code, const char* message);
        error(error_code code, const std::string& message);
        error(error_code code, std::string&& message);

        ~error();
        error(const error& other);
        error(error&& other);

        error& operator=(const error& other);
        error& operator=(error&& other);

        error_code code() const;
        const std::string& msg() const;

        explicit operator bool() const;
        operator const std::string& () const;

    private:
        error();
    };

    std::ostream& operator<<(std::ostream& os, const error& e);
    std::ostream& operator<<(std::ostream& os, const error_code& ec);

}
