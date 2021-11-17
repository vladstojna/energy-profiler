#pragma once

#include "error.hpp"

#include <util/expectedfwd.hpp>

#include <string>
#include <vector>

namespace tep
{
    // Retrieve a null-terminated string starting at address <address>
    // of a tracee with pid <pid>
    nonstd::expected<std::string, tracer_error>
        get_string(pid_t pid, uintptr_t address);

    // Retrieve multiple null-terminated strings starting at address <address>
    // of a tracee with pid <pid>
    // Useful when retrieving data with the format of the argv array
    // i.e., null-terminated array of null-terminated strings
    nonstd::expected<std::vector<std::string>, tracer_error>
        get_strings(pid_t pid, uintptr_t address);
}
