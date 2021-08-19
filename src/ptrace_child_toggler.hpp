// ptrace_child_toggler.hpp

#pragma once

#include <functional>
#include <util/expectedfwd.hpp>

namespace tep
{
    class ptrace_wrapper;
    class tracer_error;

    class ptrace_child_toggler
    {
    public:
        static nonstd::expected<ptrace_child_toggler, tracer_error>
            create(ptrace_wrapper& pw, pid_t tracer, pid_t tracee, bool trace_children);

    private:
        pid_t _tracee;
        bool _trace_children;
        std::reference_wrapper<ptrace_wrapper> _pw;

        ptrace_child_toggler(ptrace_wrapper& pw,
            pid_t tracer,
            pid_t tracee,
            bool trace_children,
            tracer_error& err) noexcept;

    public:
        ~ptrace_child_toggler() noexcept;

        ptrace_child_toggler(ptrace_child_toggler&& other) noexcept;

        ptrace_child_toggler& operator=(ptrace_child_toggler&& other) noexcept;

    };
}
