#include "ptrace_misc.hpp"
#include "ptrace_wrapper.hpp"
#include "error.hpp"

#include "nonstd/expected.hpp"

#include <cstring>
#include <string>
#include <vector>

namespace tep
{
    nonstd::expected<std::string, tracer_error>
        get_string(pid_t pid, uintptr_t address)
    {
        using rettype = decltype(ptrace(PTRACE_PEEKTEXT, 0, 0, 0));
        constexpr auto wordsz = sizeof(rettype);

        std::string str;
        str.reserve(64);
        for (auto addr = address; ; addr += wordsz)
        {
            int err;
            auto word = ptrace_wrapper::instance
                .ptrace(err, PTRACE_PEEKTEXT, pid, addr, 0);

            if (err)
            {
                return nonstd::unexpected<tracer_error>(tracer_error(
                    tracer_errcode::PTRACE_ERROR, "PTRACE_PEEKTEXT"));
            }

            char buff[wordsz + 1] = {};
            std::memcpy(buff, &word, wordsz);
            str.append(buff);
            if (std::strlen(buff) < wordsz)
                break;
        }
        return str;
    }

    nonstd::expected<std::vector<std::string>, tracer_error>
        get_strings(pid_t pid, uintptr_t address)
    {
        using rettype = decltype(ptrace(PTRACE_PEEKTEXT, 0, 0, 0));
        std::vector<std::string> vec;
        for (auto addr = address; ; addr += sizeof(rettype))
        {
            int err;
            auto word = ptrace_wrapper::instance
                .ptrace(err, PTRACE_PEEKTEXT, pid, addr, 0);
            if (err)
            {
                return nonstd::unexpected<tracer_error>(tracer_error(
                    tracer_errcode::PTRACE_ERROR, "PTRACE_PEEKTEXT"));
            }
            if (!word)
                break;

            auto str = get_string(pid, word);
            if (!str)
                return nonstd::unexpected<tracer_error>(std::move(str.error()));
            vec.push_back(*str);
        }
        return vec;
    }
}
