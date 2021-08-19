// ptrace_child_toggler.cpp

#include "ptrace_child_toggler.hpp"
#include "error.hpp"
#include "ptrace_wrapper.hpp"
#include "util.hpp"

#include <nonstd/expected.hpp>

#include <cassert>
#include <unistd.h>

using namespace tep;


nonstd::expected<ptrace_child_toggler, tracer_error>
ptrace_child_toggler::create(ptrace_wrapper& pw,
    pid_t tracer, pid_t tracee,
    bool trace_children)
{
    using rettype = nonstd::expected<ptrace_child_toggler, tracer_error>;
    tracer_error err = tracer_error::success();
    ptrace_child_toggler pct(pw, tracer, tracee, trace_children, err);
    if (err)
        return rettype(nonstd::unexpect, std::move(err));
    return pct;
}

ptrace_child_toggler::ptrace_child_toggler(ptrace_wrapper& pw,
    pid_t tracer,
    pid_t tracee,
    bool trace_children,
    tracer_error& err) noexcept :
    _tracee(tracee),
    _trace_children(trace_children),
    _pw(pw)
{
    int errnum = 0;
    if (_pw.get().ptrace(errnum, PTRACE_SETOPTIONS, _tracee, 0, get_ptrace_opts(_trace_children)) == -1)
    {
        err = get_syserror(errnum, tracer_errcode::PTRACE_ERROR, tracer, "PTRACE_SETOPTIONS");
        return;
    }
    _trace_children = !_trace_children;
}

ptrace_child_toggler::~ptrace_child_toggler() noexcept
{
    if (!_tracee)
        return;
    int errnum = 0;
    _pw.get().ptrace(errnum, PTRACE_SETOPTIONS, _tracee, 0, get_ptrace_opts(_trace_children));
    assert(errnum == 0);
}

ptrace_child_toggler::ptrace_child_toggler(ptrace_child_toggler&& other) noexcept :
    _tracee(std::exchange(other._tracee, 0)),
    _trace_children(other._trace_children),
    _pw(other._pw)
{}

ptrace_child_toggler& ptrace_child_toggler::operator=(ptrace_child_toggler&& other) noexcept
{
    _tracee = std::exchange(other._tracee, 0);
    _trace_children = other._trace_children;
    _pw = other._pw;
    return *this;
}
