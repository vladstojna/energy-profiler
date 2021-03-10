// error.hpp

#pragma once

#include "error_codes.hpp"

#include <string>

namespace nrgprf
{
    class error
    {
    public:
        static const error& success()
        {
            static error success = { error_code::SUCCESS, "Success" };
            return success;
        }

        static const error& unknown()
        {
            static error unknown = { error_code::UNKNOWN_ERROR, "Unknown error" };
            return unknown;
        }

    private:
        error_code _code;
        std::string _msg;

    public:
        error();
        error(error_code code, const char* msg);
        error(error_code code, const std::string& msg);
        error(error_code code, std::string&& msg);

        error(const error& other);
        error(error&& other);
        error& operator=(const error& other);
        error& operator=(error&& other);

        error_code code() const
        {
            return _code;
        }

        const std::string& msg() const
        {
            return _msg;
        }

        operator bool() const
        {
            return _code != error_code::SUCCESS;
        }

        friend std::ostream& operator<<(std::ostream& os, const error& e);
    };

    std::ostream& operator<<(std::ostream& os, const error& e);

}
