// registers.cpp

#include "error.hpp"
#include "ptrace_wrapper.hpp"
#include "registers.hpp"

#include <elf.h>

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
    // unlike x86_64 the IP does not advance when a breakpoint
    // has been reched, therefore no need to rewind 1 instruction
    // set_ip(get_ip() - 4);
}

#else

void cpu_gp_regs::rewind_trap() noexcept
{}

#endif // __x86_64__ || __i386__

#if defined(__x86_64__)

syscall_entry cpu_gp_regs::get_syscall_entry() const noexcept
{
    return syscall_entry{
        _regs.orig_rax,
        {
            _regs.rdi,
            _regs.rsi,
            _regs.rdx,
            _regs.r10,
            _regs.r8,
            _regs.r9
        }
    };
}

#elif defined(__i386__)

syscall_entry cpu_gp_regs::get_syscall_entry() const noexcept
{
    return syscall_entry{
        _regs.orig_eax,
        {
            _regs.ebx,
            _regs.ecx,
            _regs.edx,
            _regs.esi,
            _regs.edi,
            _regs.ebp
        }
    };
}

#elif defined(__powerpc64__)

syscall_entry cpu_gp_regs::get_syscall_entry() const noexcept
{
    return syscall_data{
        _regs.gpr[PT_R0],
        {
            _regs.gpr[PT_R3],
            _regs.gpr[PT_R4],
            _regs.gpr[PT_R5],
            _regs.gpr[PT_R6],
            _regs.gpr[PT_R7],
            _regs.gpr[PT_R8]
        }
    };
}

#endif // defined(__x86_64__)
