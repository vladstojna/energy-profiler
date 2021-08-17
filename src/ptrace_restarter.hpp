// ptrace_restarter.hpp

#include <sys/types.h>

namespace tep
{
    class tracer_error;

    class ptrace_restarter
    {
        pid_t _tid;
        pid_t _tracee;

    public:
        ptrace_restarter(pid_t tid, pid_t tracee) noexcept;
        ~ptrace_restarter() noexcept;

        tracer_error cont() noexcept;

        ptrace_restarter(ptrace_restarter&& other) noexcept;
        ptrace_restarter& operator=(ptrace_restarter&& other) noexcept;
    };
}
