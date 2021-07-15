// error.cpp

#include <nrg/error.hpp>

#include <iostream>
#include <cassert>

using namespace nrgprf;

static const std::string error_success("No error");
static const std::string error_unknown("Unknown error");
static const std::string error_no_event("No such event");

error::data::data(error_code code) :
    code(code),
    msg()
{}

error::data::data(error_code code, const char* message) :
    code(code),
    msg(message)
{
    assert(message);
}

error::data::data(error_code code, const std::string& message) :
    code(code),
    msg(message)
{}

error::data::data(error_code code, std::string&& message) :
    code(code),
    msg(std::move(message))
{}

error error::success()
{
    return {};
}

error::error() :
    _data()
{}

error::error(error_code code) :
    _data(std::make_unique<data>(code))
{}

error::error(error_code code, const char* message) :
    _data(std::make_unique<data>(code, message))
{}

error::error(error_code code, const std::string& message) :
    _data(std::make_unique<data>(code, message))
{}

error::error(error_code code, std::string&& message) :
    _data(std::make_unique<data>(code, std::move(message)))
{}

error_code error::code() const
{
    if (!_data)
        return error_code::SUCCESS;
    return _data->code;
}

const std::string& error::msg() const
{
    if (!_data)
        return error_success;
    switch (_data->code)
    {
    case error_code::SUCCESS:
        return error_success;
    case error_code::UNKNOWN_ERROR:
        return error_unknown;
    case error_code::NO_EVENT:
        return error_no_event;
    default:
        return _data->msg;
    }
}

error::operator bool() const
{
    return bool(_data) && _data->code != error_code::SUCCESS;
}

error::operator const std::string& () const
{
    return msg();
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
