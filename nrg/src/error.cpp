// error.cpp

#include "error.hpp"

#include <iostream>

using namespace nrgprf;

error::error() :
    error(error_code::SUCCESS, "Success")
{
}

error::error(error_code code, const char* msg) :
    _code(code),
    _msg(msg)
{
}

error::error(error_code code, const std::string& msg) :
    _code(code),
    _msg(msg)
{
}

error::error(error_code code, std::string&& msg) :
    _code(code),
    _msg(msg)
{
}

error::error(const error& other) = default;
error::error(error && other) = default;

error& error::operator=(const error & other) = default;
error& error::operator=(error && other) = default;

std::ostream& nrgprf::operator<<(std::ostream & os, const error & e)
{
    os << e.msg() << " (error code " << e.code() << ")";
    return os;
}
