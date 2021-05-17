// error.cpp

#include <nrg/error.hpp>

#include <iostream>
#include <cassert>
#include <cstring>


using namespace nrgprf;


static const std::string error_success("No error");
static const std::string error_unknown("Unknown error");
static const std::string error_no_event("No such event");

error error::success()
{
    return { error_code::SUCCESS };
}

error::error(error_code code) :
    _code(code),
    _msg()
{}

error::error(error_code code, const char* message) :
    _code(code),
    _msg(message)
{}

error::error(error_code code, const std::string& message) :
    _code(code),
    _msg(message)
{}

error::error(error_code code, std::string&& message) :
    _code(code),
    _msg(std::move(message))
{}

const std::string& error::msg() const
{
    switch (_code)
    {
    case error_code::SUCCESS:
        return error_success;
    case error_code::UNKNOWN_ERROR:
        return error_unknown;
    case error_code::NO_EVENT:
        return error_no_event;
    default:
        return _msg;
    }
}

error::operator bool() const
{
    return _code != error_code::SUCCESS;
}

std::ostream& nrgprf::operator<<(std::ostream& os, const error_code& ec)
{
    return os << static_cast<std::underlying_type_t<error_code>>(ec);
}

std::ostream& nrgprf::operator<<(std::ostream& os, const error& e)
{
    os << (e.msg().empty() ? "<no message>" : e.msg()) << " (error code " << e.code() << ")";
    return os;
}
