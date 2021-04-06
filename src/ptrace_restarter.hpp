// ptrace_restarter.hpp

#include <functional>
#include <sys/types.h>

namespace cmmn
{
    template<typename R, typename E>
    class expected;
}

namespace tep
{

    class ptrace_wrapper;
    class tracer_error;


    class ptrace_restarter
    {
    public:
        static cmmn::expected<ptrace_restarter, tracer_error> create();
        static cmmn::expected<ptrace_restarter, tracer_error> create(pid_t tid, pid_t tracee, ptrace_wrapper& pw);

    private:
        std::reference_wrapper<ptrace_wrapper> _pw;
        pid_t _tid;
        pid_t _tracee;
        ptrace_restarter(pid_t tid, pid_t tracee, ptrace_wrapper& pw, tracer_error& err);

    public:
        ~ptrace_restarter();

        ptrace_restarter(ptrace_restarter&& other);
        ptrace_restarter& operator=(ptrace_restarter&& other);
    };

}
