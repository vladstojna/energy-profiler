// main.cpp

#include <cstring>
#include <functional>
#include <iostream>

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


    std::ostream& write_cpu_energy(std::ostream& os,
        const std::function<nrgprf::result<uint64_t>(const nrgprf::sample&, uint8_t skt)>& func,
        const nrgprf::sample& first, const nrgprf::sample& last, uint8_t skt)
    {
        nrgprf::result<uint64_t> res_first = func(first, skt);
        nrgprf::result<uint64_t> res_last = func(last, skt);

        if (res_first && res_last)
            os << ", " << (res_last.value() - res_first.value()) * 1e-6 << " J";
        else
            os << ", N/A";
        return os;
    }


    std::ostream& operator<<(std::ostream& os, const rdr_task_pair<nrgprf::reader_gpu>& rt)
    {
        for (size_t ix = 0; ix < rt.task().size(); ix++)
        {
            const nrgprf::execution& exec = rt.task().get(ix);
            os << ix << " | ";
            os << (exec.last() - exec.first()).count() * 1e-9 << " s |";

            for (uint8_t dev = 0; dev < nrgprf::MAX_SOCKETS; dev++)
            {
                os << " dev=" << +dev;
                double total_energy = 0.0;
                for (size_t s = 1; s < exec.size(); s++)
                {
                    const nrgprf::sample& prev = exec.get(s - 1);
                    const nrgprf::sample& curr = exec.get(s);

                    nrgprf::result<uint64_t> pwr_prev = rt.reader().get_board_power(prev, dev);
                    nrgprf::result<uint64_t> pwr_curr = rt.reader().get_board_power(curr, dev);
                    if (pwr_prev && pwr_curr)
                    {
                        // mW * nS = W * 1e3 * s * 1e9 = J * 1e12
                        nrgprf::duration_t dur = curr - prev;
                        total_energy += (pwr_prev.value() + pwr_curr.value()) / 2.0 * dur.count() * 1e-12;
                    }
                    else
                        break;
                }
                if (total_energy == 0.0)
                    os << ", N/A |";
                else
                    os << ", " << total_energy << " J |";
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

            os << ix << " | ";
            os << (slast - sfirst).count() * 1e-9 << " s |";
            for (uint8_t skt = 0; skt < nrgprf::MAX_SOCKETS; skt++)
            {
                using namespace std::placeholders;
                os << " skt=" << +skt;
                write_cpu_energy(os, std::bind(&nrgprf::reader_rapl::get_pkg_energy, &rt.reader(), _1, _2), sfirst, slast, skt);
                write_cpu_energy(os, std::bind(&nrgprf::reader_rapl::get_pp0_energy, &rt.reader(), _1, _2), sfirst, slast, skt);
                write_cpu_energy(os, std::bind(&nrgprf::reader_rapl::get_pp1_energy, &rt.reader(), _1, _2), sfirst, slast, skt);
                write_cpu_energy(os, std::bind(&nrgprf::reader_rapl::get_dram_energy, &rt.reader(), _1, _2), sfirst, slast, skt);
                os << " |";
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
            os << "# End section\n";
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
