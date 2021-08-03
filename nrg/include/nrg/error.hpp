// error.hpp

#pragma once

#include <string>
#include <memory>

namespace nrgprf
{

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
