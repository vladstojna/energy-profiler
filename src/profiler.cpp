// profiler.cpp

#include "error.hpp"
#include "idle_evaluator.hpp"
#include "profiler.hpp"
#include "profiling_results.hpp"
#include "ptrace_wrapper.hpp"
#include "util.hpp"
#include "tracer.hpp"

#include <algorithm>
#include <cassert>
#include <sstream>

#include <unistd.h>
#include <sys/types.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/user.h>

#include <util/concat.hpp>


using namespace tep;


// start helper functions

tracer_error no_return_addresses(const std::string& func_name)
{
    return tracer_error(tracer_errcode::UNSUPPORTED,
        cmmn::concat("unsupported: function '", func_name,
            "' has no return addresses, possibly optimized away"));
}

// inserts trap at 'addr'
// returns either error or original word at address 'addr'
tracer_expected<long> insert_trap(pid_t my_tid, pid_t pid, uintptr_t addr)
{
    ptrace_wrapper& pw = ptrace_wrapper::instance;
    int error;
    long word = pw.ptrace(error, PTRACE_PEEKDATA, pid, addr, 0);
    if (error)
    {
        log(log_lvl::error, "[%d] error inserting trap @ 0x%" PRIxPTR, my_tid, addr);
        return get_syserror(error, tracer_errcode::PTRACE_ERROR, my_tid, "insert_trap: PTRACE_PEEKDATA");
    }
    long new_word = (word & lsb_mask()) | trap_code();
    if (pw.ptrace(error, PTRACE_POKEDATA, pid, addr, new_word) < 0)
    {
        log(log_lvl::error, "[%d] error inserting trap @ 0x%" PRIxPTR, my_tid, addr);
        return get_syserror(error, tracer_errcode::PTRACE_ERROR, my_tid, "insert_trap: PTRACE_POKEDATA");
    }
    log(log_lvl::debug, "[%d] 0x%" PRIxPTR ": %lx -> %lx", my_tid, addr, word, new_word);
    return word;
}

// end helper functions


profiler::profiler(pid_t child, const flags& flags,
    const dbg_info& dli, const config_data& cd, tracer_error& err) :
    _tid(gettid()),
    _child(child),
    _flags(flags),
    _dli(dli),
    _cd(cd),
    _readers(cd, err)
{}

profiler::profiler(pid_t child, const flags& flags,
    const dbg_info& dli, config_data&& cd, tracer_error& err) :
    _tid(gettid()),
    _child(child),
    _flags(flags),
    _dli(dli),
    _cd(std::move(cd)),
    _readers(cd, err)
{}

profiler::profiler(pid_t child, const flags& flags,
    dbg_info&& dli, const config_data& cd, tracer_error& err) :
    _tid(gettid()),
    _child(child),
    _flags(flags),
    _dli(std::move(dli)),
    _cd(cd),
    _readers(cd, err)
{}

profiler::profiler(pid_t child, const flags& flags,
    dbg_info&& dli, config_data&& cd, tracer_error& err) :
    _tid(gettid()),
    _child(child),
    _flags(flags),
    _dli(std::move(dli)),
    _cd(std::move(cd)),
    _readers(cd, err)
{}

const dbg_info& profiler::debug_line_info() const
{
    return _dli;
}

const config_data& profiler::config() const
{
    return _cd;
}

const trap_set& profiler::traps() const
{
    return _traps;
}

tracer_expected<profiling_results> profiler::run()
{
    if (_flags.obtain_idle_readings())
    {
        tracer_error err = obtain_idle_results();
        if (err)
            return err;
    }

    int wait_status;
    pid_t waited_pid = waitpid(_child, &wait_status, 0);
    if (waited_pid == -1)
        return get_syserror(errno, tracer_errcode::SYSTEM_ERROR, _tid, "waitpid");
    assert(waited_pid == _child);

    log(log_lvl::info, "[%d] started the profiling procedure for child %d", _tid, waited_pid);

    if (!WIFSTOPPED(wait_status))
    {
        log(log_lvl::error, "[%d] ptrace(PTRACE_TRACEME, ...) called but target was not stopped", _tid);
        return tracer_error(tracer_errcode::PTRACE_ERROR,
            "Tracee not stopped despite being attached with ptrace");
    }

    int errnum;
    ptrace_wrapper& pw = ptrace_wrapper::instance;
    user_regs_struct regs;
    if (pw.ptrace(errnum, PTRACE_GETREGS, waited_pid, 0, &regs) == -1)
        return get_syserror(errnum, tracer_errcode::PTRACE_ERROR, _tid, "PTRACE_GETREGS");

    uintptr_t entrypoint;
    switch (_dli.header().exec_type())
    {
    case header_info::type::dyn:
        log(log_lvl::success, "[%d] target is a PIE", _tid);
        if (get_entrypoint_addr(_child, entrypoint) == -1)
            return get_syserror(errno, tracer_errcode::SYSTEM_ERROR, _tid, "get_entrypoint_addr");
        break;
    case header_info::type::exec:
        log(log_lvl::success, "[%d] target is not a PIE", _tid);
        entrypoint = 0;
        break;
    default:
        assert(false);
    }

    log(log_lvl::info, "[%d] tracee %d rip @ 0x%" PRIxPTR ", entrypoint @ 0x%" PRIxPTR,
        _tid, waited_pid, get_ip(regs), entrypoint);

    if (pw.ptrace(errnum, PTRACE_SETOPTIONS, waited_pid, 0, get_ptrace_opts(true)) == -1)
        return get_syserror(errnum, tracer_errcode::PTRACE_ERROR, _tid, "PTRACE_SETOPTIONS");
    log(log_lvl::debug, "[%d] ptrace options successfully set", _tid);

    // iterate the sections defined in the config and insert their respective breakpoints
    if (!_dli.has_dbg_symbols())
    {
        log(log_lvl::error, "[%d] no debugging information found", _tid);
        return tracer_error(tracer_errcode::NO_SYMBOL, "No debugging information found");
    }

    for (const auto& sec : _cd.sections())
    {
        const config_data::bounds& bounds = sec.bounds();

        if (bounds.has_function())
        {
            tracer_error err = insert_traps_function(sec, bounds.func(), entrypoint);
            if (err)
                return err;
        }
        else if (bounds.has_positions())
        {
            tracer_error err = insert_traps_position(sec, bounds.start(), entrypoint);
            if (err)
                return err;
            err = insert_traps_position(sec, bounds.end(), entrypoint);
            if (err)
                return err;
        }
        else
            assert(false);
    }

    // first tracer has the same tracee tgid and tid, since there is only one tracee at this point
    tracer trc(_readers, _traps, _child, _child, entrypoint, std::launch::deferred);
    tracer_expected<gathered_results> results = trc.results();
    if (!results)
        return std::move(results.error());

    profiling_results retval(std::move(_readers), std::move(_idle));
    for (auto& [addr, execs] : results.value())
    {
        trap_set::iterator trap = _traps.find(addr);
        assert(trap != _traps.end());

        std::vector<section_results>::iterator srit =
            std::find(retval.results.begin(), retval.results.end(), trap->section());

        section_results& sr = srit == retval.results.end() ?
            retval.results.emplace_back(trap->section()) :
            *srit;

        for (auto& exec : execs)
        {
            if (exec)
            {
                sr.readings.push_back(std::move(exec.value()));
                log(log_lvl::success, "[%d] registered execution of section @ 0x%" PRIxPTR
                    " (offset = 0x%" PRIxPTR ") as successful", _tid, addr, addr - entrypoint);
            }
            else
            {
                log(log_lvl::error, "[%d] failed to gather results for execution of section @ 0x%"
                    PRIxPTR " (offset = 0x%" PRIxPTR ")", _tid, addr, addr - entrypoint);
            }
        }
    }
    return retval;
}

tracer_error profiler::obtain_idle_results()
{
    bool cpu = _cd.has_section_with(config_data::target::cpu);
    bool gpu = _cd.has_section_with(config_data::target::gpu);

    assert(cpu || gpu);
    if (!cpu && !gpu)
        return tracer_error(tracer_errcode::UNKNOWN_ERROR, "no CPU or GPU sections found");

    if (cpu)
    {
        log(log_lvl::info, "gathering idle readings for %s...", "CPU");
        idle_evaluator eval(&_readers.reader_rapl());
        auto results = eval.run();
        if (!results)
        {
            log(log_lvl::error, "unsuccessfuly gathered CPU idle readings: %s",
                results.error().msg().c_str());
            return std::move(results.error());
        }
        log(log_lvl::success, "successfuly gathered %s idle readings", "CPU");
        _idle.cpu_readings = std::move(results.value());
    }
    if (gpu)
    {
        log(log_lvl::info, "gathering idle readings for %s...", "GPU");
        idle_evaluator eval(idle_evaluator::reserve, &_readers.reader_gpu());
        auto results = eval.run();
        if (!results)
        {
            log(log_lvl::error, "unsuccessfuly gathered GPU idle readings: %s",
                results.error().msg().c_str());
            return std::move(results.error());
        }
        log(log_lvl::success, "successfuly gathered %s idle readings", "GPU");
        _idle.gpu_readings = std::move(results.value());
    }
    return tracer_error::success();
}

tracer_error profiler::insert_traps_function(const config_data::section& sec,
    const config_data::function& cfunc, uintptr_t entrypoint)
{
    dbg_expected<const function*> func_res = _dli.find_function(cfunc.name(), cfunc.cu());
    if (!func_res)
    {
        log(log_lvl::error, "[%d] function: %s", _tid, func_res.error().message.c_str());
        return tracer_error(tracer_errcode::NO_SYMBOL, std::move(func_res.error().message));
    }
    std::stringstream ss;
    const function& func = *func_res.value();
    const function_bounds& fbnds = func.bounds();

    ss << func;
    log(log_lvl::success, "[%d] found function: %s", _tid, ss.str().c_str());

    if (fbnds.returns().empty())
        return no_return_addresses(func.name());

    uintptr_t eaddr = entrypoint + fbnds.start();
    tracer_expected<long> origw = insert_trap(_tid, _child, eaddr);
    if (!origw)
        return std::move(origw.error());
    _traps.emplace(eaddr, origw.value(), sec);
    log(log_lvl::info, "[%d] inserted trap at function entry @ 0x%" PRIxPTR
        " (offset 0x%" PRIxPTR ")", _tid, eaddr, eaddr - entrypoint);

    for (uintptr_t ret : fbnds.returns())
    {
        eaddr = entrypoint + ret;
        origw = insert_trap(_tid, _child, eaddr);
        if (!origw)
            return std::move(origw.error());
        _traps.emplace(eaddr, origw.value(), sec);
        log(log_lvl::info, "[%d] inserted trap at function return @ 0x%" PRIxPTR
            " (offset 0x%" PRIxPTR ")", _tid, eaddr, eaddr - entrypoint);
    }
    return tracer_error::success();
}

tracer_error profiler::insert_traps_position(const config_data::section& sec,
    const config_data::position& p, uintptr_t entrypoint)
{
    dbg_expected<unit_lines*> ul = _dli.find_lines(p.compilation_unit());
    if (!ul)
    {
        log(log_lvl::error, "[%d] unit lines: %s", _tid, ul.error().message.c_str());
        return tracer_error(tracer_errcode::NO_SYMBOL, std::move(ul.error().message));
    }
    dbg_expected<uintptr_t> offset = ul.value()->lowest_addr(p.line());
    if (!offset)
    {
        log(log_lvl::error, "[%d] unit lines: invalid line %" PRIu32, _tid, p.line());
        return tracer_error(tracer_errcode::NO_SYMBOL, std::move(offset.error().message));
    }

    std::stringstream ss;
    ss << p;
    std::string pstr = ss.str();
    log(log_lvl::debug, "[%d] line %s offset: 0x%" PRIxPTR, _tid, pstr.c_str(), offset.value());

    uintptr_t eaddr = entrypoint + offset.value();
    tracer_expected<long> origw = insert_trap(_tid, _child, eaddr);
    if (!origw)
        return std::move(origw.error());
    _traps.emplace(eaddr, origw.value(), sec);
    log(log_lvl::info, "[%d] inserted trap @ 0x%" PRIxPTR " (offset 0x%" PRIxPTR ")",
        _tid, eaddr, eaddr - entrypoint);

    log(log_lvl::success, "[%d] inserted trap on line: %s", _tid, pstr.c_str());
    return tracer_error::success();
}
