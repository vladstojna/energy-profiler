// profiler.cpp

#include "error.hpp"
#include "profiler.hpp"
#include "ptrace_wrapper.hpp"
#include "util.hpp"
#include "tracer.hpp"

#include <algorithm>
#include <cassert>

#include <unistd.h>
#include <sys/types.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/user.h>


using namespace tep;


// start helper functions

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

tracer_error handle_reader_error(pid_t pid, const nrgprf::error& error)
{
    log(log_lvl::error, "[%d] could not create reader: %s", pid, error.msg().c_str());
    return { tracer_errcode::READER_ERROR, error.msg() };
}

// end helper functions


section_results::section_results(const config_data::section& sec) :
    section(sec),
    readings(0)
{}

bool tep::operator==(const section_results& lhs, const section_results& rhs)
{
    return lhs.section == rhs.section;
}

bool tep::operator==(const section_results& lhs, const config_data::section& rhs)
{
    return lhs.section == rhs;
}

bool tep::operator==(const config_data::section& lhs, const section_results& rhs)
{
    return lhs == rhs.section;
}


profiling_results::profiling_results(nrgprf::reader_rapl&& rr, nrgprf::reader_gpu&& rg) :
    rdr_cpu(std::move(rr)),
    rdr_gpu(std::move(rg)),
    results()
{}


profiler::profiler(pid_t child, bool pie, const dbg_line_info& dli, const config_data& cd) :
    _child(child),
    _pie(pie),
    _dli(dli),
    _cd(cd),
    _traps()
{}

profiler::profiler(pid_t child, bool pie, const dbg_line_info& dli, config_data&& cd) :
    _child(child),
    _pie(pie),
    _dli(dli),
    _cd(std::move(cd)),
    _traps()
{}

profiler::profiler(pid_t child, bool pie, dbg_line_info&& dli, const config_data& cd) :
    _child(child),
    _pie(pie),
    _dli(std::move(dli)),
    _cd(cd),
    _traps()
{}

profiler::profiler(pid_t child, bool pie, dbg_line_info&& dli, config_data&& cd) :
    _child(child),
    _pie(pie),
    _dli(std::move(dli)),
    _cd(std::move(cd)),
    _traps()
{}

const dbg_line_info& profiler::debug_line_info() const
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
    int wait_status;
    int tid = gettid();

    pid_t waited_pid = waitpid(_child, &wait_status, 0);
    if (waited_pid == -1)
        return get_syserror(errno, tracer_errcode::SYSTEM_ERROR, tid, "waitpid");
    assert(waited_pid == _child);

    log(log_lvl::info, "[%d] started the profiling procedure for child %d", tid, waited_pid);

    if (!WIFSTOPPED(wait_status))
    {
        log(log_lvl::error, "[%d] ptrace(PTRACE_TRACEME, ...) called but target was not stopped", tid);
        return tracer_error(tracer_errcode::PTRACE_ERROR,
            "Tracee not stopped despite being attached with ptrace");
    }

    int errnum;
    ptrace_wrapper& pw = ptrace_wrapper::instance;
    user_regs_struct regs;
    if (pw.ptrace(errnum, PTRACE_GETREGS, waited_pid, 0, &regs) == -1)
        return get_syserror(errnum, tracer_errcode::PTRACE_ERROR, tid, "PTRACE_GETREGS");

    uintptr_t entrypoint;
    if (get_entrypoint_addr(_pie, _child, entrypoint) == -1)
        return get_syserror(errno, tracer_errcode::SYSTEM_ERROR, tid, "get_entrypoint_addr");

    log(log_lvl::info, "[%d] tracee %d rip @ 0x%" PRIxPTR ", entrypoint @ 0x%" PRIxPTR,
        tid, waited_pid, get_ip(regs), entrypoint);

    if (pw.ptrace(errnum, PTRACE_SETOPTIONS, waited_pid, 0, get_ptrace_opts(true)) == -1)
        return get_syserror(errnum, tracer_errcode::PTRACE_ERROR, tid, "PTRACE_SETOPTIONS");
    log(log_lvl::debug, "[%d] ptrace options successfully set", tid);

    // iterate the sections defined in the config and insert their respective breakpoints
    if (!_dli.has_dbg_symbols())
    {
        log(log_lvl::error, "[%d] no debugging information found", tid);
        return tracer_error(tracer_errcode::NO_SYMBOL, "No debugging information found");
    }

    for (const auto& sec : _cd.sections())
    {
        const config_data::position& start = sec.bounds().start();
        const config_data::position& end = sec.bounds().end();

        auto start_cu = _dli.find_cu(start.compilation_unit());
        if (!start_cu)
        {
            log(log_lvl::error, "[%d] start compilation unit: %s",
                tid, start_cu.error().message.c_str());
            return tracer_error(tracer_errcode::NO_SYMBOL, std::move(start_cu.error().message));
        }
        auto end_cu = _dli.find_cu(end.compilation_unit());
        if (!end_cu)
        {
            log(log_lvl::error, "[%d] end compilation unit: %s",
                tid, end_cu.error().message.c_str());
            return tracer_error(tracer_errcode::NO_SYMBOL, std::move(end_cu.error().message));
        }

        auto start_offset = start_cu.value()->line_first_addr(start.line());
        if (!start_offset)
        {
            log(log_lvl::error, "[%d] start compilation unit: invalid line %" PRIu32, tid, start.line());
            return tracer_error(tracer_errcode::NO_SYMBOL, std::move(start_offset.error().message));
        }
        auto end_offset = end_cu.value()->line_first_addr(end.line());
        if (!end_offset)
        {
            log(log_lvl::error, "[%d] start compilation unit: invalid line %" PRIu32, tid, end.line());
            return tracer_error(tracer_errcode::NO_SYMBOL, std::move(end_offset.error().message));
        }

        log(log_lvl::debug, "[%d] start offset 0x%" PRIxPTR, tid, start_offset.value());
        log(log_lvl::debug, "[%d] end offset 0x%" PRIxPTR, tid, end_offset.value());

        uintptr_t start_addr = entrypoint + start_offset.value();
        uintptr_t end_addr = entrypoint + end_offset.value();
        auto orig_word_start = insert_trap(tid, waited_pid, start_addr);
        if (!orig_word_start)
            return std::move(orig_word_start.error());
        auto orig_word_end = insert_trap(tid, waited_pid, end_addr);
        if (!orig_word_end)
            return std::move(orig_word_end.error());
        _traps.emplace(start_addr, orig_word_start.value(), sec);
        log(log_lvl::info, "[%d] inserted trap @ 0x%" PRIxPTR " (offset 0x%" PRIxPTR ")",
            tid, start_addr, start_addr - entrypoint);
        _traps.emplace(end_addr, orig_word_end.value(), sec);
        log(log_lvl::info, "[%d] inserted trap @ 0x%" PRIxPTR " (offset 0x%" PRIxPTR ")",
            tid, end_addr, end_addr - entrypoint);
    }

    // create readers
    log(log_lvl::debug, "[%d] params: 0x%x 0x%x 0x%x", tid, _cd.parameters().domain_mask(),
        _cd.parameters().socket_mask(), _cd.parameters().device_mask());
    nrgprf::error error = nrgprf::error::success();
    nrgprf::reader_rapl rdr_cpu(static_cast<nrgprf::rapl_domain>(_cd.parameters().domain_mask() & 0xff),
        static_cast<uint8_t>(_cd.parameters().socket_mask() & 0xff), error);
    if (error)
        return handle_reader_error(tid, error);
    log(log_lvl::success, "[%d] created RAPL reader", tid);
    nrgprf::reader_gpu rdr_gpu(static_cast<uint8_t>(_cd.parameters().device_mask() & 0xff), error);
    if (error)
        return handle_reader_error(tid, error);
    log(log_lvl::success, "[%d] created GPU reader", tid);

    // first tracer has the same tracee tgid and tid, since there is only one tracee at this point
    tracer trc(_traps, _child, _child, entrypoint, rdr_cpu, rdr_gpu, std::launch::deferred);
    tracer_expected<gathered_results> results = trc.results();
    if (!results)
        return std::move(results.error());

    profiling_results retval(std::move(rdr_cpu), std::move(rdr_gpu));
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
                sr.readings.add(std::move(exec.value()));
                log(log_lvl::success, "[%d] registered execution of section @ 0x%" PRIxPTR
                    " (offset = 0x%" PRIxPTR ") as successful", tid, addr, addr - entrypoint);
            }
            else
            {
                log(log_lvl::error, "[%d] failed to gather results for execution of section @ 0x%"
                    PRIxPTR " (offset = 0x%" PRIxPTR ")", tid, addr, addr - entrypoint);
            }
        }
    }
    return retval;
}
