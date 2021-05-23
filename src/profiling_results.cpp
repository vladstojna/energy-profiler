// profiling_results.cpp

#include "profiling_results.hpp"
#include "trap.hpp"

#include <cmath>
#include <functional>
#include <iostream>
#include <iomanip>

using namespace tep;


static std::chrono::duration<double> get_duration(const timed_execution& exec)
{
    return std::chrono::duration_cast<std::chrono::duration<double>>(exec.back() - exec.front());
}

static std::ostream& operator<<(std::ostream& os, const nrgprf::joules<double>& energy)
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

static std::ostream& operator<<(std::ostream& os, const std::chrono::duration<double>& d)
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
    gpu_energy(const nrgprf::reader_gpu& rdr, const timed_execution& exec,
        uint8_t dev, board_tag) :
        _valid(false),
        _energy{}
    {
        if (exec.empty())
            return;
        for (auto it = std::next(exec.begin()); it != exec.end(); it++)
        {
            const auto& prev = *std::prev(it);
            const auto& curr = *it;

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
    cpu_energy(const nrgprf::reader_rapl& reader, const timed_execution& exec,
        uint8_t skt, Tag) :
        _energy(subtract(
            reader.get_energy<Tag>(exec.back().smp(), skt),
            reader.get_energy<Tag>(exec.front().smp(), skt)))
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
    const std::vector<pos_execs>& _task;
    const timed_execution& _idle;

public:
    rdr_task_pair(const T& reader,
        const std::vector<pos_execs>& task,
        const timed_execution& idle) :
        _reader(reader),
        _task(task),
        _idle(idle)
    {}

    const T& reader() const
    {
        return _reader;
    }

    const std::vector<pos_execs>& task() const
    {
        return _task;
    }

    const timed_execution& idle_values() const
    {
        return _idle;
    }
};

std::ostream& operator<<(std::ostream& os, const rdr_task_pair<nrgprf::reader_gpu>& rt)
{
    for (const auto& pos_exec : rt.task())
    {
        for (auto it = pos_exec.execs().begin(); it != pos_exec.execs().end(); ++it)
        {
            const auto& exec = *it;
            std::chrono::duration<double> duration = get_duration(exec);

            os << "start=" << pos_exec.start() << ", ";
            os << "end=" << pos_exec.end() << " | ";
            os << std::distance(pos_exec.execs().begin(), it) << " | " << duration;
            for (uint32_t dev = 0; dev < nrgprf::max_devices; dev++)
            {
                gpu_energy total_energy(rt.reader(), exec, dev, gpu_energy::board);
                if (!total_energy)
                    continue;

                os << " | device=" << dev << ", board=" << total_energy.get();
                if (rt.idle_values().size() >= 2)
                {
                    os << " " << idle_delta(total_energy, duration,
                        gpu_energy(rt.reader(), rt.idle_values(), dev, gpu_energy::board),
                        get_duration(rt.idle_values()));
                }
            }
            os << "\n";
        }
    }
    return os;
}

std::ostream& operator<<(std::ostream& os, const rdr_task_pair<nrgprf::reader_rapl>& rt)
{
    using namespace nrgprf;

    for (const auto& pos_exec : rt.task())
    {
        for (auto it = pos_exec.execs().begin(); it != pos_exec.execs().end(); ++it)
        {
            const auto& exec = *it;
            std::chrono::duration<double> duration = get_duration(exec);

            os << "start=" << pos_exec.start() << ", ";
            os << "end=" << pos_exec.end() << " | ";
            os << std::distance(pos_exec.execs().begin(), it) << " | " << duration;
            for (uint32_t skt = 0; skt < max_sockets; skt++)
            {
                cpu_energy pkg(rt.reader(), exec, skt, reader_rapl::package{});
                cpu_energy pp0(rt.reader(), exec, skt, reader_rapl::cores{});
                cpu_energy pp1(rt.reader(), exec, skt, reader_rapl::uncore{});
                cpu_energy dram(rt.reader(), exec, skt, reader_rapl::dram{});

                if (!pkg && !pp0 && !pp1 && !dram)
                    continue;

                os << " | socket=" << skt;
                if (pkg)
                {
                    os << ", package=" << pkg.get();
                    if (rt.idle_values().size() >= 2)
                        os << " " << idle_delta(pkg, duration,
                            cpu_energy(rt.reader(), rt.idle_values(), skt, reader_rapl::package{}),
                            get_duration(rt.idle_values()));
                }
                if (pp0)
                {
                    os << ", cores=" << pp0.get();
                    if (rt.idle_values().size() >= 2)
                        os << " " << idle_delta(pp0, duration,
                            cpu_energy(rt.reader(), rt.idle_values(), skt, reader_rapl::cores{}),
                            get_duration(rt.idle_values()));
                }
                if (pp1)
                {
                    os << ", uncore=" << pp1.get();
                    if (rt.idle_values().size() >= 2)
                        os << " " << idle_delta(pp1, duration,
                            cpu_energy(rt.reader(), rt.idle_values(), skt, reader_rapl::uncore{}),
                            get_duration(rt.idle_values()));
                }
                if (dram)
                {
                    os << ", dram=" << dram.get();
                    if (rt.idle_values().size() >= 2)
                        os << " " << idle_delta(dram, duration,
                            cpu_energy(rt.reader(), rt.idle_values(), skt, reader_rapl::dram{}),
                            get_duration(rt.idle_values()));
                }
            }
            os << "\n";
        }
    }
    return os;
}


pos_execs::pos_execs(std::unique_ptr<position_interface>&& start,
    std::unique_ptr<position_interface>&& end) :
    _start(std::move(start)),
    _end(std::move(end))
{}

void pos_execs::push_back(timed_execution&& exec)
{
    _execs.push_back(std::move(exec));
}

const position_interface& pos_execs::start() const
{
    return *_start;
}

const position_interface& pos_execs::end() const
{
    return *_end;
}

const std::vector<timed_execution>& pos_execs::execs() const
{
    return _execs;
}


result_execs::result_execs(timed_execution&& idle) :
    _idle(std::move(idle)),
    _execs()
{}

void result_execs::push_back(pos_execs&& pe)
{
    _execs.push_back(std::move(pe));
}

const std::vector<pos_execs>& result_execs::positional_execs() const
{
    return _execs;
}

const timed_execution& result_execs::idle() const
{
    return _idle;
}


template
class result_execs_dev<nrgprf::reader_rapl>;

template
class result_execs_dev<nrgprf::reader_gpu>;

template<typename Reader>
result_execs_dev<Reader>::result_execs_dev(const Reader& r, timed_execution&& idle) :
    result_execs(std::move(idle)),
    _reader(r)
{}

template<>
void result_execs_dev<nrgprf::reader_rapl>::print(std::ostream& os) const
{
    os << rdr_task_pair(_reader, positional_execs(), idle());
}

template<>
void result_execs_dev<nrgprf::reader_gpu>::print(std::ostream& os) const
{
    os << rdr_task_pair(_reader, positional_execs(), idle());
}

void profiling_results::push_back(std::unique_ptr<results_interface>&& results)
{
    _results.push_back(std::move(results));
}

std::ostream& tep::operator<<(std::ostream& os, const profiling_results& pr)
{
    for (const auto& res : pr._results)
        os << *res << "\n";
    return os;
}

std::ostream& tep::operator<<(std::ostream& os, const results_interface& ri)
{
    ri.print(os);
    return os;
}
