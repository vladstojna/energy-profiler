// profiling_results.cpp

#include "profiling_results.hpp"
#include "periodic_sampler.hpp"

#include <cmath>
#include <functional>
#include <iostream>
#include <iomanip>


static std::chrono::duration<double> get_duration(const tep::timed_execution& exec)
{
    return std::chrono::duration_cast<std::chrono::duration<double>>(exec.back() - exec.front());
}

std::ostream& operator<<(std::ostream& os, const nrgprf::joules<double>& energy)
{
    std::ios::fmtflags os_flags(os.flags());
    std::streamsize prec = os.precision();
    os << std::fixed
        << std::setprecision(std::log10(nrgprf::units_energy::ratio::den))
        << energy.count()
        << std::setprecision(prec)
        << " J";
    os.setf(os_flags);
    return os;
}

std::ostream& operator<<(std::ostream& os, const std::chrono::duration<double>& d)
{
    std::ios::fmtflags os_flags(os.flags());
    std::streamsize prec = os.precision();
    os << std::fixed
        << std::setprecision(std::log10(nrgprf::timed_sample::duration::period::den))
        << d.count()
        << std::setprecision(prec)
        << " s";
    os.setf(os_flags);
    return os;
}


class gpu_energy
{
public:
    static struct board_tag {} board;

private:
    bool _valid;
    nrgprf::joules<double> _energy;

public:
    gpu_energy(const nrgprf::reader_gpu& rdr, const tep::timed_execution& exec, uint8_t dev, board_tag tag) :
        _valid(false),
        _energy{}
    {
        (void)tag;
        for (size_t s = 1; s < exec.size(); s++)
        {
            const auto& prev = exec[s - 1];
            const auto& curr = exec[s];

            nrgprf::result<nrgprf::units_power> pwr_prev = rdr.get_board_power(prev.smp(), dev);
            nrgprf::result<nrgprf::units_power> pwr_curr = rdr.get_board_power(curr.smp(), dev);

            if (!pwr_prev || !pwr_curr)
                return;

            nrgprf::timed_sample::duration dur = curr - prev;
            nrgprf::watts<double> avg_pwr = (pwr_prev.value() + pwr_curr.value()) / 2.0;
            _energy += (avg_pwr * dur);
        }
        if (_energy.count() != 0)
            _valid = true;
    }

    const nrgprf::joules<double>& get() const
    {
        return _energy;
    }

    explicit operator bool() const
    {
        return bool(_valid);
    }
};


class cpu_energy
{
private:
    nrgprf::result<nrgprf::units_energy> _energy;

    decltype(_energy) subtract(decltype(_energy) last, decltype(_energy) first)
    {
        if (!first)
            return std::move(first.error());
        if (!last)
            return std::move(last.error());
        return last.value() - first.value();
    }

public:
    template<typename Tag>
    cpu_energy(const nrgprf::reader_rapl& reader, const tep::timed_execution& exec,
        uint8_t skt, Tag tag) :
        _energy(subtract(
            reader.get_energy(exec.back().smp(), skt, tag),
            reader.get_energy(exec.front().smp(), skt, tag)))
    {}

    const nrgprf::units_energy& get() const
    {
        return _energy.value();
    }

    explicit operator bool() const
    {
        return bool(_energy);
    }

};


class idle_delta
{
private:
    bool _write;
    nrgprf::joules<double> _energy;

    void get_energy(const nrgprf::joules<double>& normal, std::chrono::duration<double> dur,
        const nrgprf::joules<double>& idle, std::chrono::duration<double> idle_dur)
    {
        nrgprf::joules<double> norm = idle * (dur.count() / idle_dur.count());
        if (normal <= norm)
            return;
        _write = true;
        _energy = normal - norm;
    }

public:
    idle_delta(const cpu_energy& normal, const std::chrono::duration<double>& dur,
        const cpu_energy& idle, const std::chrono::duration<double>& idle_dur) :
        _write(false)
    {
        get_energy(normal.get(), dur, idle.get(), idle_dur);
    }

    idle_delta(const gpu_energy& normal, const std::chrono::duration<double>& dur,
        const gpu_energy& idle, const std::chrono::duration<double>& idle_dur) :
        _write(false)
    {
        get_energy(normal.get(), dur, idle.get(), idle_dur);
    }

    friend std::ostream& operator<<(std::ostream& os, const idle_delta& id);
};

std::ostream& operator<<(std::ostream& os, const idle_delta& id)
{
    if (id._write)
        os << "(" << id._energy << ")";
    return os;
}


template<typename T>
class rdr_task_pair
{
private:
    const T& _reader;
    const std::vector<tep::timed_execution>& _task;
    const tep::timed_execution& _idle;

public:
    rdr_task_pair(const T& reader,
        const std::vector<tep::timed_execution>& task,
        const tep::timed_execution& idle) :
        _reader(reader),
        _task(task),
        _idle(idle)
    {}

    const T& reader() const
    {
        return _reader;
    }

    const std::vector<tep::timed_execution>& task() const
    {
        return _task;
    }

    const tep::timed_execution& idle_values() const
    {
        return _idle;
    }
};

std::ostream& operator<<(std::ostream& os, const rdr_task_pair<nrgprf::reader_gpu>& rt)
{
    for (size_t ix = 0; ix < rt.task().size(); ix++)
    {
        const tep::timed_execution& exec = rt.task()[ix];
        std::chrono::duration<double> duration = get_duration(exec);

        os << std::setw(3) << ix << " | " << duration;
        for (uint8_t dev = 0; dev < nrgprf::max_sockets; dev++)
        {
            gpu_energy total_energy(rt.reader(), exec, dev, gpu_energy::board);
            if (!total_energy)
                continue;

            os << " | device=" << +dev << ", board=" << total_energy.get();
            if (rt.idle_values().size() >= 2)
            {
                os << " " << idle_delta(total_energy, duration,
                    gpu_energy(rt.reader(), rt.idle_values(), dev, gpu_energy::board),
                    get_duration(rt.idle_values()));
            }
        }
        os << "\n";
    }
    return os;
}

std::ostream& operator<<(std::ostream& os, const rdr_task_pair<nrgprf::reader_rapl>& rt)
{
    using namespace nrgprf;
    for (auto it = rt.task().begin(); it != rt.task().end(); ++it)
    {
        const auto& exec = *it;
        std::chrono::duration<double> duration = get_duration(exec);

        os << std::setw(3) << std::distance(rt.task().begin(), it) << " | " << duration;
        for (uint8_t skt = 0; skt < max_sockets; skt++)
        {
            cpu_energy pkg(rt.reader(), exec, skt, reader_rapl::package);
            cpu_energy pp0(rt.reader(), exec, skt, reader_rapl::cores);
            cpu_energy pp1(rt.reader(), exec, skt, reader_rapl::uncore);
            cpu_energy dram(rt.reader(), exec, skt, reader_rapl::dram);

            if (!pkg && !pp0 && !pp1 && !dram)
                continue;

            os << " | socket=" << +skt;
            if (pkg)
            {
                os << ", package=" << pkg.get();
                if (rt.idle_values().size() >= 2)
                    os << " " << idle_delta(pkg, duration,
                        cpu_energy(rt.reader(), rt.idle_values(), skt, reader_rapl::package),
                        get_duration(rt.idle_values()));
            }
            if (pp0)
            {
                os << ", cores=" << pp0.get();
                if (rt.idle_values().size() >= 2)
                    os << " " << idle_delta(pp0, duration,
                        cpu_energy(rt.reader(), rt.idle_values(), skt, reader_rapl::cores),
                        get_duration(rt.idle_values()));
            }
            if (pp1)
            {
                os << ", uncore=" << pp1.get();
                if (rt.idle_values().size() >= 2)
                    os << " " << idle_delta(pp1, duration,
                        cpu_energy(rt.reader(), rt.idle_values(), skt, reader_rapl::uncore),
                        get_duration(rt.idle_values()));
            }
            if (dram)
            {
                os << ", dram=" << dram.get();
                if (rt.idle_values().size() >= 2)
                    os << " " << idle_delta(dram, duration,
                        cpu_energy(rt.reader(), rt.idle_values(), skt, reader_rapl::dram),
                        get_duration(rt.idle_values()));
            }
        }
        os << "\n";
    }
    return os;
}


tep::idle_results::idle_results() :
    cpu_readings(),
    gpu_readings()
{}

tep::idle_results::idle_results(timed_execution&& cpur, timed_execution&& gpur) :
    cpu_readings(std::move(cpur)),
    gpu_readings(std::move(gpur))
{}

tep::section_results::section_results(const config_data::section& sec) :
    section(sec),
    readings()
{}

tep::profiling_results::profiling_results(reader_container&& rc, idle_results&& ir) :
    readers(std::move(rc)),
    results(),
    idle_res(std::move(ir))
{}


std::ostream& tep::operator<<(std::ostream& os, const profiling_results& pr)
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
            os << rdr_task_pair(pr.readers.reader_rapl(), sr.readings, pr.idle_res.cpu_readings);
            break;
        case config_data::target::gpu:
            os << rdr_task_pair(pr.readers.reader_gpu(), sr.readings, pr.idle_res.gpu_readings);
            break;
        }
        os << "# End readings\n";
        os << "# End section\n";
    }
    return os;
}


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
