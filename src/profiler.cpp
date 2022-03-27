// profiler.cpp
#include "profiler.hpp"
#include "error.hpp"
#include "ptrace_wrapper.hpp"
#include "util.hpp"
#include "log.hpp"
#include "tracer.hpp"
#include "registers.hpp"
#include "ptrace_misc.hpp"
#include "trap_types.hpp"
#include "dbg/utility_funcs.hpp"

#include <util/concat.hpp>
#include <nonstd/expected.hpp>

#include <unistd.h>
#include <sys/types.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/user.h>
#include <sys/syscall.h>

#include <algorithm>
#include <cassert>
#include <sstream>
#include <utility>

using namespace tep;

// start helper functions

namespace
{
    tracer_error generic_error(pid_t tid, const char* comment, std::error_code ec)
    {
        auto msg = ec.message();
        log::logline(log::error, "[%d] %s: %s", tid, comment, msg.c_str());
        return tracer_error(tracer_errcode::NO_SYMBOL, std::move(msg));
    };

    std::ostream&
        operator<<(std::ostream& os, const std::vector<std::string>& vec)
    {
        os << "[";
        for (auto it = std::begin(vec); it != std::end(vec); it++)
        {
            os << "\"" << *it << "\"";
            if (std::distance(it, std::end(vec)) > 1)
                os << ", ";
        }
        os << "]";
        return os;
    }

    template<typename T>
    std::string to_string(const T& obj)
    {
        std::stringstream ss;
        ss << obj;
        return ss.str();
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
        const cfg::section_t& section)
    {
        const nrgprf::reader* reader = readers.find(section.targets);
        const cfg::misc_attributes_t& misc = section.misc;
        assert(reader != nullptr);
        if (misc.holds<cfg::method_total_t>())
        {
            if (misc.get<cfg::method_total_t>().short_section)
                return [reader]() { return std::make_unique<short_sampler>(reader); };
            else
                return[reader]() { return std::make_unique<bounded_ps>(reader); };
        }
        else if (misc.holds<cfg::method_profile_t>())
        {
            const auto& attr = misc.get<cfg::method_profile_t>();
            const auto& interval = attr.interval;
            size_t samples = attr.samples ? *attr.samples : 384UL;
            return [reader, samples, interval]()
            {
                return std::make_unique<unbounded_ps>(reader, samples, interval);
            };
        }
        else
        {
            assert(false);
            return []() { return std::make_unique<null_async_sampler>(); };
        }
    }

    // instantiates a polymorphic results holder from config target information
    std::unique_ptr<readings_output> results_from_target(
        const reader_container& readers,
        cfg::target target)
    {
        assert(cfg::target_valid(target));
        auto create_results = [&readers](cfg::target t)
        {
            std::unique_ptr<readings_output> retval;
            switch (t)
            {
            case cfg::target::cpu:
                retval = std::make_unique<readings_output_cpu>(readers.reader_rapl());
                break;
            case cfg::target::gpu:
                retval = std::make_unique<readings_output_gpu>(readers.reader_gpu());
                break;
            };
            return retval;
        };

        if (target == cfg::target::cpu)
            return std::make_unique<readings_output_cpu>(readers.reader_rapl());
        if (target == cfg::target::gpu)
            return std::make_unique<readings_output_gpu>(readers.reader_gpu());

        std::unique_ptr<readings_output_holder> holder
            = std::make_unique<readings_output_holder>();
        for (cfg::target t = target, curr = cfg::target::cpu;
            cfg::target_valid(t);
            t &= ~curr, curr = cfg::target_next(curr))
        {
            holder->push_back(create_results(t & curr));
        }
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
                results.error().message().c_str());
            return { tracer_errcode::READER_ERROR, results.error().message() };
        }
        log::logline(log::success, "successfuly gathered %s idle readings", target);
        into = std::move(*results);
        into.shrink_to_fit();
        return tracer_error::success();
    }

    template<typename Container, typename Func>
    typename Container::iterator find_or_insert_output(
        Container& cont,
        std::optional<std::string_view> label,
        Func func)
    {
        auto it = std::find_if(cont.begin(), cont.end(),
            [label](const typename Container::value_type& val)
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

bool profiler::output_mapping::insert(start_addr bounds,
    const reader_container& readers,
    const cfg::group_t& group,
    const cfg::section_t& sec)
{
    auto grp_it = find_or_insert_output(results.groups(), group.label,
        [&group]()
        {
            return group_output{ group.label, group.extra };
        });

    auto sec_it = find_or_insert_output(grp_it->sections(), sec.label,
        [&sec, &readers]()
        {
            return section_output{
                results_from_target(readers, sec.targets),
                sec.label,
                sec.extra
            };
        });

    auto grp_begin = results.groups().begin();
    auto sec_begin = grp_it->sections().begin();
    distance_pair pair{ std::distance(grp_begin, grp_it), std::distance(sec_begin, sec_it) };

    auto [it, inserted] = map.insert({ bounds, pair });
    return inserted;
}

section_output* profiler::output_mapping::find(start_addr bounds)
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


profiler::profiler(pid_t child, flags flags,
    dbg::object_info dli, cfg::config_t cd) :
    _tid(gettid()),
    _child(child),
    _flags(std::move(flags)),
    _dli(std::move(dli)),
    _cd(std::move(cd)),
    _readers(_flags, _cd)
{}


const dbg::object_info& profiler::debug_line_info() const
{
    return _dli;
}

const cfg::config_t& profiler::config() const
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
        err, PTRACE_SETOPTIONS, _child, 0,
        PTRACE_O_TRACESYSGOOD | get_ptrace_exitkill()))
    {
        return get_syserror(err, tracer_errcode::PTRACE_ERROR,
            _tid, "PTRACE_SETOPTIONS");
    }

    for (bool entry = true, matched = false; ; )
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
            entry = !entry;
            if (matched)
                break;
            if (entry)
                continue;
            cpu_gp_regs regs(_child);
            if (auto err = regs.getregs())
                return err;
            const auto scdata = regs.get_syscall_entry();
            if (scdata.number != SYS_execve)
                continue;
            auto filename = get_string(_child, scdata.args[0]);
            if (!filename)
                return std::move(filename.error());
            auto args = get_strings(_child, scdata.args[1]);
            if (!args)
                return std::move(args.error());
            if (*filename == name)
            {
                matched = true;
                log::logline(log::success, "[%d] found matching execve: "
                    "path=%s args=%s",
                    _tid, filename->c_str(), ::to_string(*args).c_str());
            }
            else
                log::logline(log::success, "[%d] found execve: "
                    "path=%s args=%s",
                    _tid, filename->c_str(), ::to_string(*args).c_str());
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
    switch (_dli.header().type)
    {
    case dbg::executable_type::shared_object:
        log::logline(log::success, "[%d] target is a PIE", _tid);
        if (get_entrypoint_addr(_child, entrypoint) == -1)
            return system_error(_tid, "get_entrypoint_addr");
        break;
    case dbg::executable_type::executable:
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
        for (const auto& sec : group.sections)
        {
            if (sec.bounds.holds<cfg::function_t>())
            {
                if (tracer_error err = insert_traps_function(group, sec,
                    sec.bounds.get<cfg::function_t>(), entrypoint))
                    return move_error(err);
            }
            else if (sec.bounds.holds<cfg::bounds_t::position_range_t>())
            {
                auto insert_start = insert_traps_position_start(sec,
                    sec.bounds.get<cfg::bounds_t::position_range_t>().first, entrypoint);
                if (!insert_start)
                    return move_error(insert_start.error());

                if (tracer_error err = insert_traps_position_end(group, sec,
                    sec.bounds.get<cfg::bounds_t::position_range_t>().second,
                    entrypoint, *insert_start))
                    return move_error(err);
            }
            else if (sec.bounds.holds<cfg::address_range_t>())
            {
                if (tracer_error err = insert_traps_address_range(
                    group, sec, sec.bounds.get<cfg::address_range_t>(), entrypoint))
                {
                    return move_error(err);
                }
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

    for (auto& [start, end, values] : *results)
    {
        start_trap* strap = _traps.find(entrypoint + start.addr());
        assert(strap);
        if (!strap)
            return rettype(nonstd::unexpect,
                tracer_errcode::NO_TRAP,
                "Registered start traps are malformed");
        section_output* sec_out = _output.find(entrypoint + start.addr());
        assert(sec_out);
        if (!sec_out)
            return rettype(nonstd::unexpect,
                tracer_errcode::NO_TRAP,
                "Starting address not found in output map");

        if (!values)
        {
            log::logline(log::error,
                "[%d] failed to gather results for section %s - %s: %s",
                _tid,
                to_string(start).c_str(),
                to_string(end).c_str(),
                values.error().message().c_str());
        }
        else
        {
            log::logline(log::success,
                "[%d] registered execution of section %s - %s as successful",
                _tid,
                to_string(start).c_str(),
                to_string(end).c_str());
            sec_out->push_back(
                position_exec{ { start, end }, std::move(*values) });
        }
    }
    return std::move(_output.results);
}


tracer_error profiler::obtain_idle_results()
{
    auto find_section = [](const cfg::config_t& c, cfg::target t)
    {
        for (const auto& g : c.groups())
            for (const auto& s : g.sections)
                if (cfg::target_valid(s.targets & t))
                    return true;
        return false;
    };

    bool cpu = find_section(_cd, cfg::target::cpu);
    bool gpu = find_section(_cd, cfg::target::gpu);

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
    const cfg::group_t& group,
    const cfg::section_t& sec,
    const cfg::function_t& cfunc,
    uintptr_t entrypoint)
{
    auto find_function = [&, this](const cfg::function_t& f) ->
        dbg::result<std::pair<const dbg::function*, const dbg::function_symbol*>>
    {
        using unexpected = nonstd::unexpected<std::error_code>;
        if (f.compilation_unit)
        {
            auto cu = dbg::find_compilation_unit(_dli, *f.compilation_unit);
            if (!cu)
                return unexpected{ cu.error() };
            return dbg::find_function(_dli, **cu, f.name,
                dbg::exact_symbol_name_flag::no);
        }
        return dbg::find_function(_dli, f.name,
            dbg::exact_symbol_name_flag::no);
    };

    auto func_res = find_function(cfunc);
    if (!func_res)
        return generic_error(_tid, __func__, func_res.error());

    log::logline(log::info,
        "[%d] [%s] found matching function: %s declared at %s",
        _tid, __func__,
        func_res->first->die_name.c_str(),
        func_res->first->decl_loc
        ? ::to_string(*func_res->first->decl_loc).c_str()
        : "n/a");

    int inserted_traps = 0;
    if (func_res->second)
    {
        assert(func_res->first->addresses);
        log::logline(log::info, "[%d] [%s] symbol: %s",
            _tid, __func__, func_res->second->name.c_str());
        start_addr start = entrypoint + func_res->second->local_entrypoint();
        tracer_expected<long> origw = insert_trap(_tid, _child, start.val());
        if (!origw)
            return std::move(origw.error());
        auto cu = dbg::find_compilation_unit(_dli, *func_res->second);
        auto insert_res = _traps.insert(
            start,
            start_trap(
                *origw,
                trap_context{ function_call{
                    func_res->second->local_entrypoint(),
                    cu ? *cu : nullptr,
                    func_res->first,
                    func_res->second
                } },
                sec.allow_concurrency,
                creator_from_section(_readers, sec)));
        if (!insert_res.second)
        {
            log::logline(log::error,
                "[%d] trap @ 0x%" PRIxPTR " (offset 0x%" PRIxPTR ") already exists",
                _tid, start.val(), start.val() - entrypoint);
            return tracer_error(tracer_errcode::NO_TRAP,
                cmmn::concat("Trap ", ::to_string(start), " already exists"));
        }
        log::logline(log::info,
            "[%d] inserted trap at function call address 0x%" PRIxPTR " (offset 0x%" PRIxPTR ")",
            _tid, start.val(), start.val() - entrypoint);
        if (!_output.insert(start, _readers, group, sec))
            return tracer_error(tracer_errcode::NO_TRAP,
                "Trap address already exists");
        ++inserted_traps;
    }
    if (func_res->first->instances)
    {
        auto can_profile_intance = [](const dbg::inline_instance& i)
            -> std::pair<bool, dbg::contiguous_range>
        {
            auto pred = [](dbg::contiguous_range rng)
            {
                return (rng.high_pc - rng.low_pc) > 0;
            };

            auto end = i.addresses.values.end();
            auto it = std::find_if(i.addresses.values.begin(), end, pred);
            if (it == end)
                return { false, {} };
            if (end != std::find_if(it + 1, end, pred))
                return { false, {} };
            return { false, *it };
        };

        auto insert = [&](auto addr, auto creator)
        {
            auto offset = addr.val() - entrypoint;
            tracer_expected<long> origw = insert_trap(_tid, _child, addr.val());
            if (!origw)
                return std::move(origw.error());
            auto cu = dbg::find_compilation_unit(_dli, offset);
            auto insert_res = _traps.insert(addr, creator(*origw));
            if (!insert_res.second)
            {
                log::logline(log::error,
                    "[%d] trap @ 0x%" PRIxPTR " (offset 0x%" PRIxPTR ") already exists",
                    _tid, addr.val(), addr.val() - entrypoint);
                return tracer_error(tracer_errcode::NO_TRAP,
                    cmmn::concat("Trap ", ::to_string(addr), " already exists"));
            }
            log::logline(log::info,
                "[%d] inserted trap at inlined instance 0x%" PRIxPTR " (offset 0x%" PRIxPTR ")",
                _tid, addr.val(), addr.val() - entrypoint);
            return tracer_error::success();
        };

        for (const auto& inst : func_res->first->instances->insts)
        {
            assert(inst.entry_pc);
            auto [can_profile, range_idx] = can_profile_intance(inst);
            if (!can_profile)
            {
                log::logline(log::warning,
                    "[%d] [%s] unable to profile instance inlined at %s"
                    ": no or multiple contiguous ranges found",
                    _tid, __func__,
                    inst.call_loc ? ::to_string(*inst.call_loc).c_str() : "n/a");
                continue;
            }
            log::logline(log::info, "[%d] [%s] instance inlined at %s",
                _tid, __func__,
                inst.call_loc ? ::to_string(*inst.call_loc).c_str() : "n/a");

            auto cu = dbg::find_compilation_unit(_dli, range_idx.low_pc);
            start_addr start = entrypoint + range_idx.low_pc;
            end_addr end = entrypoint + range_idx.high_pc;
            inline_function start_ctx{
                range_idx.low_pc,
                cu ? *cu : nullptr,
                func_res->first,
                func_res->second,
                &inst };
            address end_ctx{
                range_idx.low_pc,
                cu ? *cu : nullptr };

            auto start_creator = [&](long origw)
            {
                return start_trap{
                    origw,
                    trap_context{ start_ctx },
                    sec.allow_concurrency,
                    creator_from_section(_readers, sec) };
            };

            auto end_creator = [&](long origw)
            {
                return end_trap{
                    origw,
                    trap_context{ end_ctx },
                    start };
            };

            if (auto err = insert(start, start_creator))
                return err;
            ++inserted_traps;
            if (auto err = insert(end, end_creator))
                return err;
            ++inserted_traps;
            if (!_output.insert(start, _readers, group, sec))
                return tracer_error(tracer_errcode::NO_TRAP,
                    "Trap address already exists");
        }
    }
    if (!inserted_traps)
    {
        log::logline(log::error,
            "[%d] [%s] unable to profile function %s declared at %s",
            _tid, __func__,
            func_res->first->die_name.c_str(),
            func_res->first->decl_loc
            ? ::to_string(*func_res->first->decl_loc).c_str()
            : "n/a");
        return tracer_error(tracer_errcode::NO_TRAP, "Unable to profile function");
    }
    return tracer_error::success();
}

tracer_error profiler::insert_traps_address_range(
    const cfg::group_t& group,
    const cfg::section_t& sec,
    const cfg::address_range_t& addr_range,
    uintptr_t entrypoint)
{
    start_addr start = entrypoint + addr_range.start;
    end_addr end = entrypoint + addr_range.end;
    tracer_expected<long> origw = insert_trap(_tid, _child, start.val());
    if (!origw)
        return std::move(origw.error());
    {
        auto cu = dbg::find_compilation_unit(_dli, addr_range.start);
        auto insert_res = _traps.insert(
            start,
            start_trap(
                *origw,
                trap_context{ address{ addr_range.start, cu ? *cu : nullptr } },
                sec.allow_concurrency,
                creator_from_section(_readers, sec)));
        if (!insert_res.second)
        {
            log::logline(log::error, "[%d] trap @ 0x%" PRIxPTR " (offset 0x%" PRIxPTR ") already exists",
                _tid, start.val(), start.val() - entrypoint);
            return tracer_error(tracer_errcode::NO_TRAP,
                cmmn::concat("Trap ", ::to_string(start), " already exists"));
        }
        log::logline(log::info, "[%d] inserted trap at start address 0x%" PRIxPTR
            " (offset 0x%" PRIxPTR ")", _tid, start.val(), start.val() - entrypoint);
    }
    origw = insert_trap(_tid, _child, end.val());
    if (!origw)
        return std::move(origw.error());
    {
        auto cu = dbg::find_compilation_unit(_dli, addr_range.end);
        auto insert_res = _traps.insert(end,
            end_trap(
                *origw,
                trap_context{ address{ addr_range.end, cu ? *cu : nullptr } },
                start));
        if (!insert_res.second)
        {
            log::logline(log::error, "[%d] trap @ 0x%" PRIxPTR " (offset 0x%" PRIxPTR ") already exists",
                _tid, end.val(), end.val() - entrypoint);
            return tracer_error(tracer_errcode::NO_TRAP,
                cmmn::concat("Trap ", ::to_string(end), " already exists"));
        }
        log::logline(log::info, "[%d] inserted trap at end address 0x%" PRIxPTR
            " (offset 0x%" PRIxPTR ")", _tid, end.val(), end.val() - entrypoint);
    }
    if (!_output.insert(start, _readers, group, sec))
        return tracer_error(tracer_errcode::NO_TRAP, "Trap address already exists");
    return tracer_error::success();
}


tracer_expected<start_addr> profiler::insert_traps_position_start(
    const cfg::section_t& sec,
    const cfg::position_t& pos,
    uintptr_t entrypoint)
{
    using unexpected = tracer_expected<start_addr>::unexpected_type;
    auto cu = dbg::find_compilation_unit(_dli, pos.compilation_unit);
    if (!cu)
        return unexpected{ generic_error(_tid, __func__, cu.error()) };
    auto lines = dbg::find_lines(**cu, (*cu)->path, pos.line,
        dbg::exact_line_value_flag::no);
    if (!lines)
        return unexpected{ generic_error(_tid, __func__, cu.error()) };
    auto line = dbg::lowest_address_line(lines->first, lines->second);
    if (!line)
        return unexpected{ generic_error(_tid, __func__, cu.error()) };

    start_addr eaddr = entrypoint + (*line)->address;
    tracer_expected<long> origw = insert_trap(_tid, _child, eaddr.val());
    if (!origw)
        return unexpected{ std::move(origw).error() };
    log::logline(log::info, "[%d] inserted trap @ 0x%" PRIxPTR " (offset 0x%" PRIxPTR ")",
        _tid, eaddr.val(), eaddr.val() - entrypoint);

    auto insert_res = _traps.insert(eaddr,
        start_trap(
            *origw,
            trap_context{ source_line{ (*line)->address, *cu, (*line) } },
            sec.allow_concurrency,
            creator_from_section(_readers, sec)));
    if (!insert_res.second)
    {
        log::logline(log::error, "[%d] trap @ 0x%" PRIxPTR " (offset 0x%" PRIxPTR ") already exists",
            _tid, eaddr.val(), eaddr.val() - entrypoint);
        return unexpected{ tracer_error{
            tracer_errcode::NO_TRAP,
            cmmn::concat("Trap ", ::to_string(eaddr), " already exists") } };
    }

    log::logline(log::debug, "[%d] line %s @ offset 0x%" PRIxPTR,
        _tid, ::to_string(**line).c_str(), (*line)->number);
    log::logline(log::success, "[%d] inserted trap on line: %s",
        _tid, ::to_string(**line).c_str());
    return eaddr;
}

tracer_error profiler::insert_traps_position_end(
    const cfg::group_t& group,
    const cfg::section_t& sec,
    const cfg::position_t& pos,
    uintptr_t entrypoint,
    start_addr start)
{
    auto cu = dbg::find_compilation_unit(_dli, pos.compilation_unit);
    if (!cu)
        return generic_error(_tid, __func__, cu.error());
    auto lines = dbg::find_lines(**cu, (*cu)->path, pos.line,
        dbg::exact_line_value_flag::no);
    if (!lines)
        return generic_error(_tid, __func__, cu.error());
    auto line = dbg::lowest_address_line(lines->first, lines->second);
    if (!line)
        return generic_error(_tid, __func__, cu.error());

    end_addr eaddr = entrypoint + (*line)->address;
    tracer_expected<long> origw = insert_trap(_tid, _child, eaddr.val());
    if (!origw)
        return std::move(origw.error());
    log::logline(log::info, "[%d] inserted trap @ 0x%" PRIxPTR " (offset 0x%" PRIxPTR ")",
        _tid, eaddr.val(), eaddr.val() - entrypoint);

    auto insert_res = _traps.insert(eaddr,
        end_trap(
            *origw,
            trap_context{ source_line{ (*line)->address, *cu, *line } },
            start));
    if (!insert_res.second)
    {
        log::logline(log::error, "[%d] trap @ 0x%" PRIxPTR " (offset 0x%" PRIxPTR ") already exists",
            _tid, eaddr.val(), eaddr.val() - entrypoint);
        return tracer_error(tracer_errcode::NO_TRAP,
            cmmn::concat("Trap ", ::to_string(eaddr), " already exists"));
    }
    if (!_output.insert(start, _readers, group, sec))
        return tracer_error(tracer_errcode::NO_TRAP, "Trap address already exists");

    log::logline(log::debug, "[%d] line %s @ offset 0x%" PRIxPTR,
        _tid, ::to_string(**line).c_str(), (*line)->number);
    log::logline(log::success, "[%d] inserted trap on line: %s",
        _tid, ::to_string(**line).c_str());
    return tracer_error::success();
}
