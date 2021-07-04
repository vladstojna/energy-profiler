// registers.cpp

#include "error.hpp"
#include "ptrace_wrapper.hpp"
#include "registers.hpp"

using namespace tep;


cpu_gp_regs::cpu_gp_regs(pid_t pid) :
    _pid(pid),
    _iov{ &_regs, sizeof(_regs) },
    _regs{}
{}

tracer_error cpu_gp_regs::getregs()
{
    int errnum;
    ptrace_wrapper& pw = ptrace_wrapper::instance;
    if (pw.ptrace(errnum, PTRACE_GETREGSET, _pid, NT_PRSTATUS, &_iov) == -1)
        return get_syserror(errnum, tracer_errcode::PTRACE_ERROR, _pid, "PTRACE_GETREGSET");
    return tracer_error::success();
}

tracer_error cpu_gp_regs::setregs()
{
    int errnum;
    ptrace_wrapper& pw = ptrace_wrapper::instance;
    if (pw.ptrace(errnum, PTRACE_SETREGSET, _pid, NT_PRSTATUS, &_iov) == -1)
        return get_syserror(errnum, tracer_errcode::PTRACE_ERROR, _pid, "PTRACE_SETREGSET");
    return tracer_error::success();
}


#ifdef __x86_64__

uintptr_t cpu_gp_regs::get_ip() const noexcept
{
    return _regs.rip;
}

void cpu_gp_regs::set_ip(uintptr_t addr) noexcept
{
    _regs.rip = addr;
}

#elif defined(__i386__)

uintptr_t cpu_gp_regs::get_ip() const noexcept
{
    return _regs.eip;
}

void cpu_gp_regs::set_ip(uintptr_t addr) noexcept
{
    _regs.eip = addr;
}

#elif defined(__powerpc64__)

uintptr_t cpu_gp_regs::get_ip() const noexcept
{
    return _regs.nip;
}

void cpu_gp_regs::set_ip(uintptr_t addr) noexcept
{
    _regs.nip = addr;
}

#else

uintptr_t cpu_gp_regs::get_ip() const noexcept
{
    return 0;
}

void cpu_gp_regs::set_ip(uintptr_t) noexcept
{}

#endif // __x86_64__


#if defined(__x86_64__) || defined(__i386__)

void cpu_gp_regs::rewind_trap() noexcept
{
    set_ip(get_ip() - 1);
}

#elif defined(__powerpc64__)

void cpu_gp_regs::rewind_trap() noexcept
{
    set_ip(get_ip() - 4);
}

#else

void cpu_gp_regs::rewind_trap() noexcept
{}

#endif // __x86_64__ || __i386__