// profiler.cpp

#include "error.hpp"
#include "profiler.hpp"
#include "ptrace_wrapper.hpp"
#include "util.hpp"
#include "log.hpp"
#include "tracer.hpp"
#include "registers.hpp"
#include "ptrace_misc.hpp"

#include <algorithm>
#include <cassert>
#include <sstream>
#include <utility>

#include <unistd.h>
#include <sys/types.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/user.h>
#include <sys/syscall.h>

#include <util/concat.hpp>
#include <nonstd/expected.hpp>

using namespace tep;

// start helper functions

namespace
{
    template<typename T>
    std::string to_string(const T& obj)
    {
        std::stringstream ss;
        ss << obj;
        return ss.str();
    }

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
        using rettype = tracer_expected<long>;
        auto ptrace_error = [](int code, pid_t tid, const char* comment)
        {
            return rettype(nonstd::unexpect,
                get_syserror(code, tracer_errcode::PTRACE_ERROR, tid, comment));
        };
        ptrace_wrapper& pw = ptrace_wrapper::instance;
        int error;
        long word = pw.ptrace(error, PTRACE_PEEKDATA, pid, addr, 0);
        if (error)
        {
            log::logline(log::error, "[%d] error inserting trap @ 0x%" PRIxPTR, my_tid, addr);
            return ptrace_error(error, my_tid, "insert_trap: PTRACE_PEEKDATA");
        }
        long new_word = set_trap(word);
        if (pw.ptrace(error, PTRACE_POKEDATA, pid, addr, new_word) < 0)
        {
            log::logline(log::error, "[%d] error inserting trap @ 0x%" PRIxPTR, my_tid, addr);
            return ptrace_error(error, my_tid, "insert_trap: PTRACE_POKEDATA");
        }
        log::logline(log::debug, "[%d] 0x%" PRIxPTR ": %lx -> %lx", my_tid, addr, word, new_word);
        return word;
    }

    // instantiates a polymorphic sampler_creator from config section information
    sampler_creator creator_from_section(const reader_container& readers,
        const config_data::section& section)
    {
        const nrgprf::reader* reader = readers.find(section.targets());
        const std::chrono::milliseconds& interval = section.interval();
        size_t samples = section.samples();

        assert(reader != nullptr);
        switch (section.method())
        {
        case config_data::profiling_method::energy_profile:
        {
            return [reader, interval, samples]()
            {
                return std::make_unique<unbounded_ps>(reader, samples, interval);
            };
        } break;
        case config_data::profiling_method::energy_total:
        {
            if (section.is_short())
            {
                return [reader]()
                {
                    return std::make_unique<short_sampler>(reader);
                };
            }
            else
            {
                return [reader, interval]()
                {
                    return std::make_unique<bounded_ps>(reader, interval);
                };
            }
        } break;
        default:
            assert(false);
        }
        return []() { return std::make_unique<null_async_sampler>(); };
    }

    // instantiates a polymorphic results holder from config target information
    std::unique_ptr<readings_output> results_from_target(
        const reader_container& readers,
        config_data::target target)
    {
        switch (target)
        {
        case config_data::target::cpu:
            return std::make_unique<readings_output_cpu>(readers.reader_rapl());
        case config_data::target::gpu:
            return std::make_unique<readings_output_gpu>(readers.reader_gpu());
        default:
            assert(false);
        }
        return {};
    }

    std::unique_ptr<readings_output> results_from_targets(
        const reader_container& readers,
        const config_data::section::target_cont& targets)
    {
        assert(!targets.empty());
        if (targets.size() == 1)
            return results_from_target(readers, *targets.begin());
        std::unique_ptr<readings_output_holder> holder = std::make_unique<readings_output_holder>();
        for (auto tgt : targets)
            holder->push_back(results_from_target(readers, tgt));
        return holder;
    }

    tracer_error sample_idle(const char* target, const nrgprf::reader* reader, timed_execution& into)
    {
        assert(target);
        assert(reader);
        constexpr static const std::chrono::milliseconds default_sleep(5000);
        constexpr static const std::chrono::milliseconds default_period(40);
        constexpr static const size_t initial_size = default_sleep / default_period + 100;

        auto sleep_func = []()
        {
            log::logline(log::info, "sleeping for %lu milliseconds", default_sleep.count());
            std::this_thread::sleep_for(default_sleep);
        };
        log::logline(log::info, "gathering idle readings for %s...", target);

        // reserve enough initially in order to avoid future allocations
        auto results = async_sampler_fn(
            std::make_unique<unbounded_ps>(reader, initial_size, default_period), sleep_func)
            .run();
        if (!results)
        {
            log::logline(log::error, "unsuccessfuly gathered %s idle readings: %s", target,
                results.error().msg().c_str());
            return { tracer_errcode::READER_ERROR, results.error().msg() };
        }
        log::logline(log::success, "successfuly gathered %s idle readings", target);
        into = std::move(*results);
        into.shrink_to_fit();
        return tracer_error::success();
    }

    template<typename Container, typename Func>
    typename Container::iterator find_or_insert_output(
        Container& cont,
        const std::string& label,
        Func func)
    {
        auto it = std::find_if(cont.begin(), cont.end(),
            [&label](const typename Container::value_type& val)
            {
                return label == val.label();
            });

        if (it == cont.end())
        {
            cont.push_back(func());
            it = std::prev(cont.end());
        }

        assert(it != cont.end());
        return it;
    }
}

// end helper functions

bool profiler::output_mapping::insert(addr_bounds bounds,
    const reader_container& readers,
    const config_data::section_group& group,
    const config_data::section& sec)
{
    auto grp_it = find_or_insert_output(results.groups(), group.label(),
        [&group]()
        {
            return group_output{ group.label(), group.extra() };
        });

    auto sec_it = find_or_insert_output(grp_it->sections(), sec.label(),
        [&sec, &readers]()
        {
            return section_output{
                results_from_targets(readers, sec.targets()), sec.label(), sec.extra()
            };
        });

    auto grp_begin = results.groups().begin();
    auto sec_begin = grp_it->sections().begin();
    distance_pair pair{ std::distance(grp_begin, grp_it), std::distance(sec_begin, sec_it) };

    auto [it, inserted] = map.insert({ bounds, pair });
    return inserted;
}

section_output* profiler::output_mapping::find(addr_bounds bounds)
{
    auto it = map.find(bounds);
    assert(it != map.end());
    if (it == map.end())
        return nullptr;

    auto distance_group = it->second.first;
    auto distance_sec = it->second.second;

    auto grp_it = results.groups().begin();
    assert(std::distance(grp_it, results.groups().end()) > distance_group);
    std::advance(grp_it, distance_group);

    auto sec_it = grp_it->sections().begin();
    assert(std::distance(sec_it, grp_it->sections().end()) > distance_sec);
    std::advance(sec_it, distance_sec);

    return &*sec_it;
}


profiler::profiler(pid_t child, const flags& flags,
    const dbg_info& dli, const config_data& cd, tracer_error& err) :
    _tid(gettid()),
    _child(child),
    _flags(flags),
    _dli(dli),
    _cd(cd),
    _readers(_flags, _cd, err)
{}

profiler::profiler(pid_t child, const flags& flags,
    const dbg_info& dli, config_data&& cd, tracer_error& err) :
    _tid(gettid()),
    _child(child),
    _flags(flags),
    _dli(dli),
    _cd(std::move(cd)),
    _readers(_flags, _cd, err)
{}

profiler::profiler(pid_t child, const flags& flags,
    dbg_info&& dli, const config_data& cd, tracer_error& err) :
    _tid(gettid()),
    _child(child),
    _flags(flags),
    _dli(std::move(dli)),
    _cd(cd),
    _readers(_flags, _cd, err)
{}

profiler::profiler(pid_t child, const flags& flags,
    dbg_info&& dli, config_data&& cd, tracer_error& err) :
    _tid(gettid()),
    _child(child),
    _flags(flags),
    _dli(std::move(dli)),
    _cd(std::move(cd)),
    _readers(_flags, _cd, err)
{}

const dbg_info& profiler::debug_line_info() const
{
    return _dli;
}

const config_data& profiler::config() const
{
    return _cd;
}

const registered_traps& profiler::traps() const
{
    return _traps;
}


tracer_error profiler::await_executable(const std::string& name) const
{
    auto system_error = [](pid_t tid, const char* comment, int errnum = errno)
    {
        return get_syserror(errnum, tracer_errcode::SYSTEM_ERROR, tid, comment);
    };

    int opts = PTRACE_O_TRACESYSGOOD;
    int wait_status;
    pid_t waited_pid = waitpid(_child, &wait_status, 0);
    if (waited_pid == -1)
        return system_error(_tid, "waitpid");
    assert(waited_pid == _child);
    if (WIFEXITED(wait_status))
    {
        log::logline(log::error, "[%d] failed to run target in child %d",
            _tid, _child);
        return tracer_error(tracer_errcode::SIGNAL_DURING_SECTION_ERROR,
            "Child failed to run target");
    }
    log::logline(log::info, "[%d] started the profiling procedure for child %d",
        _tid, _child);
    if (!WIFSTOPPED(wait_status))
    {
        log::logline(log::error, "[%d] ptrace(PTRACE_TRACEME, ...) "
            "called but target was not stopped", _tid);
        return tracer_error(tracer_errcode::PTRACE_ERROR,
            "Tracee not stopped despite being attached with ptrace");
    }

    if (int err; -1 == ptrace_wrapper::instance.ptrace(
        err, PTRACE_SETOPTIONS, _child, 0, opts))
    {
        return get_syserror(err, tracer_errcode::PTRACE_ERROR,
            _tid, "PTRACE_SETOPTIONS");
    }

    for (bool entry = true, matching = false; ; )
    {
        if (int err; -1 == ptrace_wrapper::instance.ptrace(
            err, PTRACE_SYSCALL, _child, 0, 0))
        {
            return get_syserror(err, tracer_errcode::PTRACE_ERROR,
                _tid, "PTRACE_SYSCALL");
        }

        if (pid_t waited = waitpid(_child, &wait_status, 0); waited == -1)
            return system_error(_tid, "waitpid");

        if (is_syscall_trap(wait_status))
        {
            cpu_gp_regs regs(_child);
            if (auto err = regs.getregs())
                return err;

            const auto scdata = regs.get_syscall_entry();
            if (scdata.number != SYS_execve)
                continue;
            if (entry)
            {
                auto filename = get_string(_child, scdata.args[0]);
                if (!filename)
                    return std::move(filename.error());
                auto args = get_strings(_child, scdata.args[1]);
                if (!args)
                    return std::move(args.error());
                if (*filename == name)
                {
                    matching = true;
                    log::logline(log::success, "[%d] found matching execve: %s",
                        _tid, filename->c_str());
                }
                else
                {
                    log::logline(log::info, "[%d] found execve: %s",
                        _tid, filename->c_str());
                }
            }
            else if (matching)
                break;
            entry = !entry;
        }
        else if (WIFEXITED(wait_status))
        {
            log::logline(log::error, "[%d] child %d exited with status %d",
                _tid, _child, WEXITSTATUS(wait_status));
            return tracer_error(tracer_errcode::UNKNOWN_ERROR,
                cmmn::concat("Child exited before executing ", name));
        }
        else if (WIFSIGNALED(wait_status))
        {
            log::logline(log::error, "[%d] child %d signaled: %s",
                _tid, _child, sig_str(WTERMSIG(wait_status)));
            return tracer_error(tracer_errcode::UNKNOWN_ERROR,
                cmmn::concat("Child signaled before executing ", name));
        }
    }
    // send SIGSTOP to child for run() to behave transparently
    if (kill(_child, SIGSTOP) == -1)
        return system_error(_tid, "kill");
    // continue the tracee execution
    if (int err; -1 == ptrace_wrapper::instance.ptrace(
        err, PTRACE_CONT, _child, 0, 0))
    {
        return system_error(_tid, "waitpid");
    }
    return tracer_error::success();
}


tracer_expected<profiling_results> profiler::run()
{
    using rettype = tracer_expected<profiling_results>;

    auto system_error = [](pid_t tid, const char* comment)
    {
        return rettype(nonstd::unexpect,
            get_syserror(errno, tracer_errcode::SYSTEM_ERROR, tid, comment));
    };
    auto move_error = [](tracer_error& err)
    {
        return rettype(nonstd::unexpect, std::move(err));
    };

    int wait_status;
    pid_t waited_pid = waitpid(_child, &wait_status, 0);
    if (waited_pid == -1)
        return system_error(_tid, "waitpid");
    assert(waited_pid == _child);
    if (WIFEXITED(wait_status))
    {
        log::logline(log::error, "[%d] failed to run target in child %d", _tid, waited_pid);
        return rettype(nonstd::unexpect,
            tracer_errcode::SIGNAL_DURING_SECTION_ERROR,
            "Child failed to run target");
    }
    log::logline(log::info, "[%d] started the profiling procedure for child %d", _tid, waited_pid);
    if (!WIFSTOPPED(wait_status))
    {
        log::logline(log::error, "[%d] ptrace(PTRACE_TRACEME, ...) "
            "called but target was not stopped", _tid);
        return rettype(nonstd::unexpect,
            tracer_errcode::PTRACE_ERROR,
            "Tracee not stopped despite being attached with ptrace");
    }

    if (_flags.obtain_idle)
        if (tracer_error err = obtain_idle_results())
            return rettype(nonstd::unexpect, std::move(err));
    cpu_gp_regs regs(waited_pid);
    if (tracer_error err = regs.getregs())
        return move_error(err);
    uintptr_t entrypoint;
    switch (_dli.header().exec_type())
    {
    case header_info::type::dyn:
        log::logline(log::success, "[%d] target is a PIE", _tid);
        if (get_entrypoint_addr(_child, entrypoint) == -1)
            return system_error(_tid, "get_entrypoint_addr");
        break;
    case header_info::type::exec:
        log::logline(log::success, "[%d] target is not a PIE", _tid);
        entrypoint = 0;
        break;
    default:
        assert(false);
    }

    log::logline(log::info, "[%d] tracee %d rip @ 0x%" PRIxPTR ", entrypoint @ 0x%" PRIxPTR,
        _tid, waited_pid, regs.get_ip(), entrypoint);

    int errnum;
    if (ptrace_wrapper::instance
        .ptrace(errnum, PTRACE_SETOPTIONS, waited_pid, 0, get_ptrace_opts(true)) == -1)
    {
        return rettype(nonstd::unexpect,
            get_syserror(errnum, tracer_errcode::PTRACE_ERROR, _tid, "PTRACE_SETOPTIONS"));
    }
    log::logline(log::debug, "[%d] ptrace options successfully set", _tid);

    // iterate the sections defined in the config and insert their respective breakpoints
    for (const auto& group : _cd.groups())
    {
        for (const auto& sec : group.sections())
        {
            if (sec.bounds().has_function())
            {
                if (tracer_error err = insert_traps_function(group, sec,
                    sec.bounds().func(), entrypoint))
                    return move_error(err);
            }
            else if (sec.bounds().has_positions())
            {
                if (!_dli.has_line_info())
                {
                    log::logline(log::error, "[%d] no line information found", _tid);
                    return rettype(nonstd::unexpect,
                        tracer_errcode::NO_SYMBOL, "No line information found");
                }

                auto insert_start = insert_traps_position_start(sec,
                    sec.bounds().start(), entrypoint);
                if (!insert_start)
                    return move_error(insert_start.error());

                if (tracer_error err = insert_traps_position_end(group, sec,
                    sec.bounds().end(), entrypoint, *insert_start))
                    return move_error(err);
            }
            else
                assert(false);
        }
    }

    // first tracer has the same tracee tgid and tid, since there is only one tracee at this point
    tracer trc(_traps, _child, _child, entrypoint, std::launch::deferred);
    auto results = trc.results();
    if (!results)
        return move_error(results.error());

    for (auto& [pair, execs] : *results)
    {
        start_trap* strap = _traps.find(pair.first);
        end_trap* etrap = _traps.find(pair.second, pair.first);

        assert(strap && etrap);
        if (!strap || !etrap)
            return rettype(nonstd::unexpect,
                tracer_errcode::NO_TRAP, "Starting or ending traps are malformed");
        section_output* sec_out = _output.find(pair);
        assert(sec_out != nullptr);
        if (sec_out == nullptr)
            return rettype(nonstd::unexpect,
                tracer_errcode::NO_TRAP, "Address bounds not found");

        pos::interval interval{ std::move(strap->at()), std::move(etrap->at()) };
        std::string interval_str = to_string(interval);
        for (auto& exec : execs)
        {
            if (!exec)
            {
                log::logline(log::error, "[%d] failed to gather results for section %s: %s",
                    _tid, interval_str.c_str(), exec.error().msg().c_str());
                continue;
            }
            log::logline(log::success, "[%d] registered execution of section %s as successful",
                _tid, interval_str.c_str());
            sec_out->push_back({ interval, std::move(*exec) });
        }
    }
    return std::move(_output.results);
}


tracer_error profiler::obtain_idle_results()
{
    bool cpu = _cd.has_section_with(config_data::target::cpu);
    bool gpu = _cd.has_section_with(config_data::target::gpu);

    assert(cpu || gpu);
    if (!cpu && !gpu)
        return tracer_error(tracer_errcode::UNKNOWN_ERROR, "no CPU or GPU sections found");
    if (timed_execution into; cpu)
    {
        if (tracer_error err = sample_idle("CPU", &_readers.reader_rapl(), into))
            return err;
        _output.results.idle().emplace_back(
            std::make_unique<readings_output_cpu>(_readers.reader_rapl()), std::move(into));
    }
    if (timed_execution into; gpu)
    {
        if (tracer_error err = sample_idle("GPU", &_readers.reader_gpu(), into))
            return err;
        _output.results.idle().emplace_back(
            std::make_unique<readings_output_gpu>(_readers.reader_gpu()), std::move(into));
    }
    return tracer_error::success();
}


tracer_error profiler::insert_traps_function(
    const config_data::section_group& group,
    const config_data::section& sec,
    const config_data::function& cfunc,
    uintptr_t entrypoint)
{
    dbg_expected<const function*> func_res = _dli.find_function(cfunc.name(), cfunc.cu());
    if (!func_res)
    {
        log::logline(log::error, "[%d] function: %s", _tid, func_res.error().message.c_str());
        return tracer_error(tracer_errcode::NO_SYMBOL, std::move(func_res.error().message));
    }
    const function& func = **func_res;
    const function_bounds& fbnds = func.bounds();

    pos::function pf{ func.name() };
    std::string pos_func_str = to_string(pf);

    log::logline(log::success, "[%d] found function: %s", _tid, pos_func_str.c_str());

    if (fbnds.returns().empty())
        return no_return_addresses(pos_func_str);

    start_addr start = entrypoint + fbnds.start();
    tracer_expected<long> origw = insert_trap(_tid, _child, start.val());
    if (!origw)
        return std::move(origw.error());

    log::logline(log::info, "[%d] inserted trap at function %s entry @ 0x%" PRIxPTR
        " (offset 0x%" PRIxPTR ")",
        _tid, pos_func_str.c_str(), start.val(), start.val() - entrypoint);

    auto insert_res = _traps.insert(start,
        start_trap(*origw, std::move(pf), sec.allow_concurrency(),
            creator_from_section(_readers, sec)));

    if (!insert_res.second)
    {
        log::logline(log::error, "[%d] trap @ 0x%" PRIxPTR " (offset 0x%" PRIxPTR ") already exists",
            _tid, start.val(), start.val() - entrypoint);
        return tracer_error(tracer_errcode::NO_TRAP,
            cmmn::concat("Trap ", to_string(start), " already exists"));
    }

    for (uintptr_t ret : fbnds.returns())
    {
        uintptr_t offset = ret - fbnds.start();

        pos::offset pf_off{ pos::function{func.name()}, offset };
        pos_func_str = to_string(pf_off);

        end_addr end = entrypoint + ret;
        origw = insert_trap(_tid, _child, end.val());
        if (!origw)
            return std::move(origw.error());
        log::logline(log::info, "[%d] inserted trap at function return @ %s",
            _tid, pos_func_str.c_str());

        auto insert_res = _traps.insert(
            end,
            end_trap(*origw, pos::single_pos{ std::move(pf_off) }, start));
        if (!insert_res.second)
        {
            log::logline(log::error, "[%d] trap @ 0x%" PRIxPTR " (offset 0x%" PRIxPTR ") already exists",
                _tid, end.val(), end.val() - entrypoint);
            return tracer_error(tracer_errcode::NO_TRAP,
                cmmn::concat("Trap ", std::to_string(end.val()), " already exists"));
        }
        if (!_output.insert({ start, end }, _readers, group, sec))
            return tracer_error(tracer_errcode::NO_TRAP, "Trap address interval already exists");
    }
    return tracer_error::success();
}


tracer_expected<start_addr> profiler::insert_traps_position_start(
    const config_data::section& sec,
    const config_data::position& pos,
    uintptr_t entrypoint)
{
    using rettype = tracer_expected<start_addr>;
    dbg_expected<unit_lines*> ul = _dli.find_lines(pos.compilation_unit());
    if (!ul)
    {
        log::logline(log::error, "[%d] unit lines: %s", _tid, ul.error().message.c_str());
        return rettype(nonstd::unexpect,
            tracer_errcode::NO_SYMBOL, std::move(ul.error().message));
    }
    dbg_expected<std::pair<uint32_t, uintptr_t>> line_addr = (*ul)->lowest_addr(pos.line());
    if (!line_addr)
    {
        log::logline(log::error, "[%d] unit lines: invalid line %" PRIu32, _tid, pos.line());
        return rettype(nonstd::unexpect,
            tracer_errcode::NO_SYMBOL, std::move(line_addr.error().message));
    }

    pos::line posline{ (*ul)->name(), line_addr->first };
    std::string pstr = to_string(posline);

    start_addr eaddr = entrypoint + line_addr->second;
    tracer_expected<long> origw = insert_trap(_tid, _child, eaddr.val());
    if (!origw)
        return rettype(nonstd::unexpect, std::move(origw.error()));
    log::logline(log::info, "[%d] inserted trap @ 0x%" PRIxPTR " (offset 0x%" PRIxPTR ")",
        _tid, eaddr.val(), eaddr.val() - entrypoint);

    auto insert_res = _traps.insert(eaddr,
        start_trap(*origw, std::move(posline), sec.allow_concurrency(),
            creator_from_section(_readers, sec)));
    if (!insert_res.second)
    {
        log::logline(log::error, "[%d] trap @ 0x%" PRIxPTR " (offset 0x%" PRIxPTR ") already exists",
            _tid, eaddr.val(), eaddr.val() - entrypoint);
        return rettype(nonstd::unexpect,
            tracer_errcode::NO_TRAP,
            cmmn::concat("Trap ", to_string(eaddr), " already exists"));
    }

    log::logline(log::debug, "[%d] line %s @ offset 0x%" PRIxPTR,
        _tid, pstr.c_str(), line_addr->second);
    log::logline(log::success, "[%d] inserted trap on line: %s", _tid, pstr.c_str());
    return eaddr;
}

tracer_error profiler::insert_traps_position_end(
    const config_data::section_group& group,
    const config_data::section& sec,
    const config_data::position& pos,
    uintptr_t entrypoint,
    start_addr start)
{
    dbg_expected<unit_lines*> ul = _dli.find_lines(pos.compilation_unit());
    if (!ul)
    {
        log::logline(log::error, "[%d] unit lines: %s", _tid, ul.error().message.c_str());
        return tracer_error(tracer_errcode::NO_SYMBOL, std::move(ul.error().message));
    }
    dbg_expected<std::pair<uint32_t, uintptr_t>> line_addr = (*ul)->lowest_addr(pos.line());
    if (!line_addr)
    {
        log::logline(log::error, "[%d] unit lines: invalid line %" PRIu32, _tid, pos.line());
        return tracer_error(tracer_errcode::NO_SYMBOL, std::move(line_addr.error().message));
    }

    pos::line posline = { (*ul)->name(), line_addr->first };
    std::string pstr = to_string(posline);

    end_addr eaddr = entrypoint + line_addr->second;
    tracer_expected<long> origw = insert_trap(_tid, _child, eaddr.val());
    if (!origw)
        return std::move(origw.error());
    log::logline(log::info, "[%d] inserted trap @ 0x%" PRIxPTR " (offset 0x%" PRIxPTR ")",
        _tid, eaddr.val(), eaddr.val() - entrypoint);

    auto insert_res = _traps.insert(eaddr, end_trap(*origw, std::move(posline), start));
    if (!insert_res.second)
    {
        log::logline(log::error, "[%d] trap @ 0x%" PRIxPTR " (offset 0x%" PRIxPTR ") already exists",
            _tid, eaddr.val(), eaddr.val() - entrypoint);
        return tracer_error(tracer_errcode::NO_TRAP,
            cmmn::concat("Trap ", std::to_string(eaddr.val()), " already exists"));
    }
    if (!_output.insert({ start, eaddr }, _readers, group, sec))
        return tracer_error(tracer_errcode::NO_TRAP, "Trap address interval already exists");

    log::logline(log::debug, "[%d] line %s @ offset 0x%" PRIxPTR,
        _tid, pstr.c_str(), line_addr->second);
    log::logline(log::success, "[%d] inserted trap on line: %s", _tid, pstr.c_str());
    return tracer_error::success();
}

template<typename D, typename C>
nonstd::expected<profiler, tracer_error> profiler::create(pid_t child, const flags& f,
    D&& dli, C&& cd)
{
    using rettype = nonstd::expected<profiler, tracer_error>;
    tracer_error err = tracer_error::success();
    profiler prof(child, f, std::forward<D>(dli), std::forward<C>(cd), err);
    if (err)
        return rettype(nonstd::unexpect, std::move(err));
    return prof;
}

template nonstd::expected<profiler, tracer_error>
profiler::create(pid_t child, const flags& f, dbg_info&& dli, config_data&& cd);

template nonstd::expected<profiler, tracer_error>
profiler::create(pid_t child, const flags& f, dbg_info&& dli, const config_data& cd);

template nonstd::expected<profiler, tracer_error>
profiler::create(pid_t child, const flags& f, const dbg_info& dli, config_data&& cd);

template nonstd::expected<profiler, tracer_error>
profiler::create(pid_t child, const flags& f, const dbg_info& dli, const config_data& cd);
