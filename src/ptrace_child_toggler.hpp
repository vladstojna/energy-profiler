// ptrace_child_toggler.hpp

#pragma once

#include <functional>

namespace cmmn
{
    template<typename R, typename E>
    class expected;
}

namespace tep
{

    class ptrace_wrapper;

    class tracer_error;

    class ptrace_child_toggler
    {
    public:
        static cmmn::expected<ptrace_child_toggler, tracer_error> create(ptrace_wrapper& pw,
            pid_t tracer, pid_t tracee,
            bool trace_children);

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
