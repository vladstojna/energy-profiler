// main.cpp

#include <cmath>
#include <cstring>
#include <functional>
#include <iostream>
#include <iomanip>

#include "cmdargs.hpp"
#include "dbg.hpp"
#include "error.hpp"
#include "profiler.hpp"
#include "ptrace_wrapper.hpp"
#include "target.hpp"
#include "util.hpp"

namespace tep
{

    template<typename T>
    class rdr_task_pair
    {
    private:
        const T* _reader;
        const nrgprf::task* _task;

    public:
        rdr_task_pair(const T& reader, const nrgprf::task& task) :
            _reader(&reader),
            _task(&task)
        {}

        const T& reader() const { return *_reader; }
        const nrgprf::task& task() const { return *_task; }
    };

    using get_value_fn = nrgprf::result<nrgprf::units_energy>(nrgprf::reader_rapl::*)(const nrgprf::sample&, uint8_t) const;

    std::chrono::duration<double> get_duration(const nrgprf::execution& exec)
    {
        return std::chrono::duration_cast<std::chrono::duration<double>>(exec.last() - exec.first());
    }

    nrgprf::result<nrgprf::units_energy> get_cpu_energy(get_value_fn func, const nrgprf::reader_rapl& rdr,
        const nrgprf::sample& first, const nrgprf::sample& last, uint8_t skt)
    {
        nrgprf::result<nrgprf::units_energy> res_first = std::invoke(func, rdr, first, skt);
        nrgprf::result<nrgprf::units_energy> res_last = std::invoke(func, rdr, last, skt);
        if (!res_first)
            return std::move(res_first.error());
        if (!res_last)
            return std::move(res_last.error());
        return res_last.value() - res_first.value();
    }

    std::ostream& operator<<(std::ostream& os, const nrgprf::joules<double>& energy)
    {
        std::ios::fmtflags os_flags(os.flags());
        std::streamsize prec = os.precision();
        os << std::fixed
            << std::setprecision(std::log10(nrgprf::units_energy::ratio::den))
            << energy.count()
            << std::setprecision(prec)
            << " (J)";
        os.setf(os_flags);
        return os;
    }

    std::ostream& operator<<(std::ostream& os, const std::chrono::duration<double>& d)
    {
        std::ios::fmtflags os_flags(os.flags());
        std::streamsize prec = os.precision();
        os << std::fixed
            << std::setprecision(std::log10(nrgprf::duration_t::period::den))
            << d.count()
            << std::setprecision(prec)
            << " (s)";
        os.setf(os_flags);
        return os;
    }

    std::ostream& operator<<(std::ostream& os, const rdr_task_pair<nrgprf::reader_gpu>& rt)
    {
        for (size_t ix = 0; ix < rt.task().size(); ix++)
        {
            const nrgprf::execution& exec = rt.task().get(ix);
            os << std::setw(3) << ix << " | ";
            os << get_duration(exec);

            for (uint8_t dev = 0; dev < nrgprf::MAX_SOCKETS; dev++)
            {
                nrgprf::joules<double> total_energy{};
                for (size_t s = 1; s < exec.size(); s++)
                {
                    const nrgprf::sample& prev = exec.get(s - 1);
                    const nrgprf::sample& curr = exec.get(s);

                    nrgprf::result<nrgprf::units_power> pwr_prev = rt.reader().get_board_power(prev, dev);
                    nrgprf::result<nrgprf::units_power> pwr_curr = rt.reader().get_board_power(curr, dev);

                    if (!pwr_prev || !pwr_curr)
                        break;

                    nrgprf::duration_t dur = curr - prev;
                    nrgprf::watts<double> avg_pwr = (pwr_prev.value() + pwr_curr.value()) / 2.0;
                    total_energy += (avg_pwr * dur);
                }
                if (total_energy.count() != 0)
                    os << " | device=" << +dev << ", board=" << total_energy;
            }
            os << "\n";
        }
        return os;
    }

    std::ostream& operator<<(std::ostream& os, const rdr_task_pair<nrgprf::reader_rapl>& rt)
    {
        for (size_t ix = 0; ix < rt.task().size(); ix++)
        {
            const nrgprf::execution& exec = rt.task().get(ix);
            const nrgprf::sample& sfirst = exec.first();
            const nrgprf::sample& slast = exec.last();

            os << std::setw(3) << ix << " | ";
            os << get_duration(exec);
            for (uint8_t skt = 0; skt < nrgprf::MAX_SOCKETS; skt++)
            {
                nrgprf::result<nrgprf::units_energy> pkg = get_cpu_energy(
                    &nrgprf::reader_rapl::get_pkg_energy,
                    rt.reader(), sfirst, slast, skt);
                nrgprf::result<nrgprf::units_energy> pp0 = get_cpu_energy(
                    &nrgprf::reader_rapl::get_pp0_energy,
                    rt.reader(), sfirst, slast, skt);
                nrgprf::result<nrgprf::units_energy> pp1 = get_cpu_energy(
                    &nrgprf::reader_rapl::get_pp1_energy,
                    rt.reader(), sfirst, slast, skt);
                nrgprf::result<nrgprf::units_energy> dram = get_cpu_energy(
                    &nrgprf::reader_rapl::get_dram_energy,
                    rt.reader(), sfirst, slast, skt);

                if (!pkg && !pp0 && !pp1 && !dram)
                    continue;

                os << " | socket=" << +skt;
                if (pkg)
                    os << ", package=" << pkg.value();
                if (pp0)
                    os << ", cores=" << pp0.value();
                if (pp1)
                    os << ", uncore=" << pp1.value();
                if (dram)
                    os << ", dram=" << dram.value();
            }
            os << "\n";
        }
        return os;
    }

    std::ostream& operator<<(std::ostream& os, const profiling_results& pr)
    {
        os << "# Results\n";
        for (const auto& sr : pr.results)
        {
            os << "# Begin section\n";
            os << sr.section << "\n";
            os << "# Begin readings\n";
            switch (sr.section.target())
            {
            case config_data::target::cpu:
                os << rdr_task_pair(pr.rdr_cpu, sr.readings);
                break;
            case config_data::target::gpu:
                os << rdr_task_pair(pr.rdr_gpu, sr.readings);
                break;
            }
            os << "# End readings\n";
            os << "# End section\n";
        }
        return os;
    }

}

int main(int argc, char* argv[])
{
    using namespace tep;
    cmmn::expected<arguments, arg_error> args = parse_arguments(argc, argv);
    if (!args)
        return 1;

    int idx = args.value().target_index();

    dbg_expected<dbg_line_info> dbg_info = dbg_line_info::create(argv[idx]);
    if (!dbg_info)
    {
        std::cerr << dbg_info.error() << std::endl;
        return 1;
    }

    cfg_result config = load_config(args.value().config());
    if (!config)
    {
        std::cerr << config.error() << std::endl;
        return 1;
    }

    std::cout << args.value() << "\n";
    std::cout << dbg_info.value() << "\n";
    std::cout << config.value() << std::endl;

    int errnum;
    pid_t child_pid = ptrace_wrapper::instance.fork(errnum, &run_target, &argv[idx]);
    if (child_pid > 0)
    {
        profiler profiler(child_pid, args.value().pie(), std::move(dbg_info.value()), std::move(config.value()));
        cmmn::expected<profiling_results, tracer_error> results = profiler.run();
        if (!results)
        {
            std::cerr << results.error() << std::endl;
            return 1;
        }
        args.value().outfile().stream() << results.value();
        return 0;
    }
    else if (child_pid == -1)
    {
        log(log_lvl::error, "fork(): %s", strerror(errnum));
    }
    return 1;
}
