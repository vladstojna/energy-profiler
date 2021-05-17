// error.hpp

#pragma once

#include <string>

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
        INVALID_DOMAIN_NAME,
        UNKNOWN_ERROR,
    };

    // error holder

    class error
    {
    public:
        static error success();

    private:
        error_code _code;
        std::string _msg;

    public:
        error(error_code code);
        error(error_code code, const char* message);
        error(error_code code, const std::string& message);
        error(error_code code, std::string&& message);

        error_code code() const { return _code; }
        const std::string& msg() const;

        explicit operator bool() const;
    };

    std::ostream& operator<<(std::ostream& os, const error& e);
    std::ostream& operator<<(std::ostream& os, const error_code& ec);

}
