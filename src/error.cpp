// error.cpp

#include "error.hpp"
#include "util.hpp"

#include <cstring>
#include <iostream>


using namespace tep;


static const std::string msg_success("No error");
static const std::string msg_unknown("Unknown error");


tracer_error::tracer_error(tracer_errcode code) :
    _code(code),
    _msg()
{}

tracer_error::tracer_error(tracer_errcode code, const char* msg) :
    _code(code),
    _msg(msg)
{}

tracer_error::tracer_error(tracer_errcode code, std::string&& msg) :
    _code(code),
    _msg(std::move(msg))
{}

tracer_error::tracer_error(tracer_errcode code, const std::string& msg) :
    _code(code),
    _msg(msg)
{}

tracer_errcode tracer_error::code() const
{
    return _code;
}

const std::string& tracer_error::msg() const
{
    switch (_code)
    {
    case tracer_errcode::SUCCESS:
        return msg_success;
    case tracer_errcode::UNKNOWN_ERROR:
        return msg_unknown;
    default:
        return _msg;
    }
}

tracer_error::operator bool() const
{
    return _code != tracer_errcode::SUCCESS;
}


std::ostream& tep::operator<<(std::ostream& os, const tracer_errcode& code)
{
    return os << static_cast<std::underlying_type_t<tracer_errcode>>(code);
}

std::ostream& tep::operator<<(std::ostream& os, const tracer_error& e)
{
    os << (e.msg().empty() ? "<no message>" : e.msg());
    os << " (error code " << e.code() << ")";
    return os;
}


tracer_error tep::get_syserror__(const char* file, int line, int errnum,
    tracer_errcode code, pid_t tid, const char* comment)
{
    char error_msg[256];
    char* msg = strerror_r(errnum, error_msg, sizeof(error_msg));
    log__(file, line, log_lvl::error, "[%d] %s: %s", tid, comment, msg);
    return { code, msg };
}
