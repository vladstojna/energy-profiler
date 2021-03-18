// error.hpp

#pragma once

#include <string>

namespace tep
{

    // error codes

    enum class tracer_errcode
    {
        SUCCESS,
        SYSTEM_ERROR,
        PTRACE_ERROR,
        READER_ERROR,
        UNKNOWN_ERROR,
    };


    // error holder

    class tracer_error
    {
    public:
        static tracer_error success()
        {
            return { tracer_errcode::SUCCESS };
        }

    private:
        tracer_errcode _code;
        std::string _msg;

    public:
        tracer_error(tracer_errcode code);
        tracer_error(tracer_errcode code, const char* msg);
        tracer_error(tracer_errcode code, std::string&& msg);
        tracer_error(tracer_errcode code, const std::string& msg);

        tracer_errcode code() const;
        const std::string& msg() const;

        explicit operator bool() const;
    };


    // operator overloads

    std::ostream& operator<<(std::ostream& os, const tracer_errcode& code);
    std::ostream& operator<<(std::ostream& os, const tracer_error& e);

};
