// profiler.cpp

#include "error.hpp"
#include "profiler.hpp"
#include "ptrace_wrapper.hpp"
#include "util.hpp"
#include "tracer.hpp"
#include "registers.hpp"

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

template<typename T>
static std::string to_string(const T& obj)
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
    ptrace_wrapper& pw = ptrace_wrapper::instance;
    int error;
    long word = pw.ptrace(error, PTRACE_PEEKDATA, pid, addr, 0);
    if (error)
    {
        log(log_lvl::error, "[%d] error inserting trap @ 0x%" PRIxPTR, my_tid, addr);
        return get_syserror(error, tracer_errcode::PTRACE_ERROR, my_tid, "insert_trap: PTRACE_PEEKDATA");
    }
    long new_word = set_trap(word);
    if (pw.ptrace(error, PTRACE_POKEDATA, pid, addr, new_word) < 0)
    {
        log(log_lvl::error, "[%d] error inserting trap @ 0x%" PRIxPTR, my_tid, addr);
        return get_syserror(error, tracer_errcode::PTRACE_ERROR, my_tid, "insert_trap: PTRACE_POKEDATA");
    }
    log(log_lvl::debug, "[%d] 0x%" PRIxPTR ": %lx -> %lx", my_tid, addr, word, new_word);
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
        return [reader, interval]()
        {
            return std::make_unique<bounded_ps>(reader, interval);
        };
    } break;
    default:
        assert(false);
    }
    return []() { return std::make_unique<null_async_sampler>(); };
}

// instantiates a polymorphic results holder from config target information
std::unique_ptr<readings_output>
results_from_target(const reader_container& readers,
    const idle_results& idle_res,
    config_data::target target)
{
    switch (target)
    {
    case config_data::target::cpu:
        return std::make_unique<readings_output_cpu>(readers.reader_rapl(), idle_res.cpu_readings);
    case config_data::target::gpu:
        return std::make_unique<readings_output_gpu>(readers.reader_gpu(), idle_res.gpu_readings);
    default:
        assert(false);
    }
    return {};
}

std::unique_ptr<readings_output>
results_from_targets(const reader_container& readers,
    const idle_results& idle_res,
    const config_data::section::target_cont& targets)
{
    assert(!targets.empty());
    if (targets.size() == 1)
        return results_from_target(readers, idle_res, *targets.begin());
    std::unique_ptr<readings_output_holder> holder = std::make_unique<readings_output_holder>();
    for (auto tgt : targets)
        holder->push_back(results_from_target(readers, idle_res, tgt));
    return holder;
}

// end helper functions


bool profiler::output_mapping::insert(addr_bounds bounds,
    const reader_container& readers,
    const idle_results& idle,
    const config_data::section_group& group,
    const config_data::section& sec)
{
    distance_pair pair{};
    const std::string& label = group.label();

    auto grp_it = std::find_if(results.groups().begin(), results.groups().end(),
        [&label](const group_output& go)
        {
            return label == go.label();
        });

    if (grp_it == results.groups().end())
    {
        results.groups().push_back({ group.label() });
        grp_it = std::prev(results.groups().end());
    }
    assert(grp_it != results.groups().end());

    grp_it->push_back({
        results_from_targets(readers, idle, sec.targets()),
        sec.label(),
        sec.extra() });

    auto grp_begin = results.groups().begin();
    auto sec_begin = grp_it->sections().begin();
    auto sec_it = std::prev(grp_it->sections().end());
    pair = { std::distance(grp_begin, grp_it), std::distance(sec_begin, sec_it) };

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
    _readers(_cd, err)
{}

profiler::profiler(pid_t child, const flags& flags,
    const dbg_info& dli, config_data&& cd, tracer_error& err) :
    _tid(gettid()),
    _child(child),
    _flags(flags),
    _dli(dli),
    _cd(std::move(cd)),
    _readers(_cd, err)
{}

profiler::profiler(pid_t child, const flags& flags,
    dbg_info&& dli, const config_data& cd, tracer_error& err) :
    _tid(gettid()),
    _child(child),
    _flags(flags),
    _dli(std::move(dli)),
    _cd(cd),
    _readers(_cd, err)
{}

profiler::profiler(pid_t child, const flags& flags,
    dbg_info&& dli, config_data&& cd, tracer_error& err) :
    _tid(gettid()),
    _child(child),
    _flags(flags),
    _dli(std::move(dli)),
    _cd(std::move(cd)),
    _readers(_cd, err)
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

    cpu_gp_regs regs(waited_pid);
    if (tracer_error err = regs.getregs())
        return err;

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
        _tid, waited_pid, regs.get_ip(), entrypoint);

    int errnum;
    if (ptrace_wrapper::instance
        .ptrace(errnum, PTRACE_SETOPTIONS, waited_pid, 0, get_ptrace_opts(true)) == -1)
    {
        return get_syserror(errnum, tracer_errcode::PTRACE_ERROR, _tid, "PTRACE_SETOPTIONS");
    }
    log(log_lvl::debug, "[%d] ptrace options successfully set", _tid);

    // iterate the sections defined in the config and insert their respective breakpoints
    for (const auto& group : _cd.groups())
    {
        for (const auto& sec : group.sections())
        {
            if (sec.bounds().has_function())
            {
                tracer_error err = insert_traps_function(group, sec,
                    sec.bounds().func(), entrypoint);
                if (err)
                    return err;
            }
            else if (sec.bounds().has_positions())
            {
                if (!_dli.has_line_info())
                {
                    log(log_lvl::error, "[%d] no line information found", _tid);
                    return tracer_error(tracer_errcode::NO_SYMBOL, "No line information found");
                }

                auto insert_start = insert_traps_position_start(sec,
                    sec.bounds().start(), entrypoint);
                if (!insert_start)
                    return std::move(insert_start.error());

                tracer_error err = insert_traps_position_end(group, sec,
                    sec.bounds().end(), entrypoint,
                    insert_start.value());
                if (err)
                    return err;
            }
            else
                assert(false);
        }
    }

    // first tracer has the same tracee tgid and tid, since there is only one tracee at this point
    tracer trc(_traps, _child, _child, entrypoint, std::launch::deferred);
    tracer_expected<tracer::gathered_results> results = trc.results();
    if (!results)
        return std::move(results.error());

    for (auto& [pair, execs] : results.value())
    {
        start_trap* strap = _traps.find(pair.first);
        end_trap* etrap = _traps.find(pair.second, pair.first);

        assert(strap && etrap);
        if (!strap || !etrap)
            return tracer_error(tracer_errcode::NO_TRAP, "Starting or ending traps are malformed");
        section_output* sec_out = _output.find(pair);
        assert(sec_out != nullptr);
        if (sec_out == nullptr)
            return tracer_error(tracer_errcode::NO_TRAP, "Address bounds not found");

        std::shared_ptr<position_interval> interval =
            std::make_shared<position_interval>(
                std::move(*strap).at(), std::move(*etrap).at());
        std::string interval_str = to_string(*interval);

        for (auto& exec : execs)
        {
            if (!exec)
            {
                log(log_lvl::error, "[%d] failed to gather results for section %s: %s",
                    _tid, interval_str.c_str(), exec.error().msg().c_str());
                continue;
            }
            log(log_lvl::success, "[%d] registered execution of section %s as successful",
                _tid, interval_str.c_str());
            sec_out->push_back({ interval, std::move(exec.value()) });
        }
    }
    return std::move(_output.results);
}


tracer_error profiler::obtain_idle_results()
{
    static std::chrono::milliseconds default_sleep(5000);

    bool cpu = _cd.has_section_with(config_data::target::cpu);
    bool gpu = _cd.has_section_with(config_data::target::gpu);

    assert(cpu || gpu);
    if (!cpu && !gpu)
        return tracer_error(tracer_errcode::UNKNOWN_ERROR, "no CPU or GPU sections found");

    auto sleep_func = []()
    {
        log(log_lvl::info, "sleeping for %lu milliseconds", default_sleep.count());
        std::this_thread::sleep_for(default_sleep);
    };

    if (cpu)
    {
        log(log_lvl::info, "gathering idle readings for %s...", "CPU");

        auto results = sync_sampler_fn(&_readers.reader_rapl(), sleep_func).run();
        if (!results)
        {
            log(log_lvl::error, "unsuccessfuly gathered CPU idle readings: %s",
                results.error().msg().c_str());
            return { tracer_errcode::READER_ERROR, results.error().msg() };
        }
        log(log_lvl::success, "successfuly gathered %s idle readings", "CPU");
        _idle.cpu_readings = std::move(results.value());
    }
    if (gpu)
    {
        log(log_lvl::info, "gathering idle readings for %s...", "GPU");

        // reserve enough initially in order to avoid future allocations
        size_t initial_size = default_sleep / unbounded_ps::default_period + 100;
        auto results = async_sampler_fn(
            std::make_unique<unbounded_ps>(&_readers.reader_gpu(), initial_size),
            sleep_func)
            .run();

        if (!results)
        {
            log(log_lvl::error, "unsuccessfuly gathered GPU idle readings: %s",
                results.error().msg().c_str());
            return { tracer_errcode::READER_ERROR, results.error().msg() };
        }
        log(log_lvl::success, "successfuly gathered %s idle readings", "GPU");
        _idle.gpu_readings = std::move(results.value());
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
        log(log_lvl::error, "[%d] function: %s", _tid, func_res.error().message.c_str());
        return tracer_error(tracer_errcode::NO_SYMBOL, std::move(func_res.error().message));
    }
    const function& func = *func_res.value();
    const function_bounds& fbnds = func.bounds();

    std::unique_ptr<position_func> pf = std::make_unique<position_func>(func.name());
    std::string pos_func_str = to_string(*pf);

    log(log_lvl::success, "[%d] found function: %s", _tid, pos_func_str.c_str());

    if (fbnds.returns().empty())
        return no_return_addresses(pos_func_str);

    start_addr start = entrypoint + fbnds.start();
    tracer_expected<long> origw = insert_trap(_tid, _child, start.val());
    if (!origw)
        return std::move(origw.error());

    log(log_lvl::info, "[%d] inserted trap at function %s entry @ 0x%" PRIxPTR
        " (offset 0x%" PRIxPTR ")",
        _tid, pos_func_str.c_str(), start.val(), start.val() - entrypoint);

    auto insert_res = _traps.insert(start,
        start_trap(origw.value(), std::move(pf), sec.allow_concurrency(),
            creator_from_section(_readers, sec)));

    if (!insert_res.second)
    {
        log(log_lvl::error, "[%d] trap @ 0x%" PRIxPTR " (offset 0x%" PRIxPTR ") already exists",
            _tid, start.val(), start.val() - entrypoint);
        return tracer_error(tracer_errcode::NO_TRAP,
            cmmn::concat("Trap ", to_string(start), " already exists"));
    }

    for (uintptr_t ret : fbnds.returns())
    {
        uintptr_t offset = ret - fbnds.start();
        std::unique_ptr<position_offset> pf_off = std::make_unique<position_offset>(
            std::make_unique<position_func>(func.name()),
            offset);
        pos_func_str = to_string(*pf_off);

        end_addr end = entrypoint + ret;
        origw = insert_trap(_tid, _child, end.val());
        if (!origw)
            return std::move(origw.error());
        log(log_lvl::info, "[%d] inserted trap at function return @ %s",
            _tid, pos_func_str.c_str());

        auto insert_res = _traps.insert(end, end_trap(origw.value(), std::move(pf_off), start));
        if (!insert_res.second)
        {
            log(log_lvl::error, "[%d] trap @ 0x%" PRIxPTR " (offset 0x%" PRIxPTR ") already exists",
                _tid, end.val(), end.val() - entrypoint);
            return tracer_error(tracer_errcode::NO_TRAP,
                cmmn::concat("Trap ", std::to_string(end.val()), " already exists"));
        }
        if (!_output.insert({ start, end }, _readers, _idle, group, sec))
            return tracer_error(tracer_errcode::NO_TRAP, "Trap address interval already exists");
    }
    return tracer_error::success();
}


cmmn::expected<start_addr, tracer_error> profiler::insert_traps_position_start(
    const config_data::section& sec,
    const config_data::position& pos,
    uintptr_t entrypoint)
{
    dbg_expected<unit_lines*> ul = _dli.find_lines(pos.compilation_unit());
    if (!ul)
    {
        log(log_lvl::error, "[%d] unit lines: %s", _tid, ul.error().message.c_str());
        return tracer_error(tracer_errcode::NO_SYMBOL, std::move(ul.error().message));
    }
    dbg_expected<std::pair<uint32_t, uintptr_t>> line_addr = ul.value()->lowest_addr(pos.line());
    if (!line_addr)
    {
        log(log_lvl::error, "[%d] unit lines: invalid line %" PRIu32, _tid, pos.line());
        return tracer_error(tracer_errcode::NO_SYMBOL, std::move(line_addr.error().message));
    }

    std::unique_ptr<position_line> posline = std::make_unique<position_line>(
        ul.value()->name(), line_addr.value().first);
    std::string pstr = to_string(*posline);

    start_addr eaddr = entrypoint + line_addr.value().second;
    tracer_expected<long> origw = insert_trap(_tid, _child, eaddr.val());
    if (!origw)
        return std::move(origw.error());
    log(log_lvl::info, "[%d] inserted trap @ 0x%" PRIxPTR " (offset 0x%" PRIxPTR ")",
        _tid, eaddr.val(), eaddr.val() - entrypoint);

    auto insert_res = _traps.insert(eaddr,
        start_trap(origw.value(), std::move(posline), sec.allow_concurrency(),
            creator_from_section(_readers, sec)));
    if (!insert_res.second)
    {
        log(log_lvl::error, "[%d] trap @ 0x%" PRIxPTR " (offset 0x%" PRIxPTR ") already exists",
            _tid, eaddr.val(), eaddr.val() - entrypoint);
        return tracer_error(tracer_errcode::NO_TRAP,
            cmmn::concat("Trap ", to_string(eaddr), " already exists"));
    }

    log(log_lvl::debug, "[%d] line %s @ offset 0x%" PRIxPTR,
        _tid, pstr.c_str(), line_addr.value().second);
    log(log_lvl::success, "[%d] inserted trap on line: %s", _tid, pstr.c_str());
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
        log(log_lvl::error, "[%d] unit lines: %s", _tid, ul.error().message.c_str());
        return tracer_error(tracer_errcode::NO_SYMBOL, std::move(ul.error().message));
    }
    dbg_expected<std::pair<uint32_t, uintptr_t>> line_addr = ul.value()->lowest_addr(pos.line());
    if (!line_addr)
    {
        log(log_lvl::error, "[%d] unit lines: invalid line %" PRIu32, _tid, pos.line());
        return tracer_error(tracer_errcode::NO_SYMBOL, std::move(line_addr.error().message));
    }

    std::unique_ptr<position_line> posline = std::make_unique<position_line>(
        ul.value()->name(), line_addr.value().first);
    std::string pstr = to_string(*posline);

    end_addr eaddr = entrypoint + line_addr.value().second;
    tracer_expected<long> origw = insert_trap(_tid, _child, eaddr.val());
    if (!origw)
        return std::move(origw.error());
    log(log_lvl::info, "[%d] inserted trap @ 0x%" PRIxPTR " (offset 0x%" PRIxPTR ")",
        _tid, eaddr.val(), eaddr.val() - entrypoint);

    auto insert_res = _traps.insert(eaddr, end_trap(origw.value(), std::move(posline), start));
    if (!insert_res.second)
    {
        log(log_lvl::error, "[%d] trap @ 0x%" PRIxPTR " (offset 0x%" PRIxPTR ") already exists",
            _tid, eaddr.val(), eaddr.val() - entrypoint);
        return tracer_error(tracer_errcode::NO_TRAP,
            cmmn::concat("Trap ", std::to_string(eaddr.val()), " already exists"));
    }
    if (!_output.insert({ start, eaddr }, _readers, _idle, group, sec))
        return tracer_error(tracer_errcode::NO_TRAP, "Trap address interval already exists");

    log(log_lvl::debug, "[%d] line %s @ offset 0x%" PRIxPTR,
        _tid, pstr.c_str(), line_addr.value().second);
    log(log_lvl::success, "[%d] inserted trap on line: %s", _tid, pstr.c_str());
    return tracer_error::success();
}
