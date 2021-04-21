// profiling_results.cpp

#include "profiling_results.hpp"

#include <cmath>
#include <functional>
#include <iostream>
#include <iomanip>
#include <sstream>


static constexpr const char sum_str[] = "sum";
static constexpr const char avg_str[] = "avg";
static constexpr const char outer_sep[] = " | ";
static constexpr const char inner_sep[] = ", ";

std::chrono::duration<double> get_duration(const nrgprf::execution& exec)
{
    return std::chrono::duration_cast<std::chrono::duration<double>>(exec.last() - exec.first());
}

constexpr size_t get_num_digits(size_t num)
{
    size_t digits = 1;
    if (num < 10)
        return digits;
    while (num >= 10)
    {
        num /= 10;
        digits++;
    }
    return digits;
}

constexpr size_t get_padding(size_t count)
{
    size_t max = std::max(sizeof(sum_str) - 1, sizeof(avg_str) - 1);
    return std::max(max, count);
}

bool socket_has_events(const nrgprf::reader_rapl& rdr, uint8_t skt)
{
    return rdr.event_idx(nrgprf::rapl_domain::PKG, skt) != -1 ||
        rdr.event_idx(nrgprf::rapl_domain::PP0, skt) != -1 ||
        rdr.event_idx(nrgprf::rapl_domain::PP1, skt) != -1 ||
        rdr.event_idx(nrgprf::rapl_domain::DRAM, skt) != -1;
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
        << std::setprecision(std::log10(nrgprf::duration_t::period::den))
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
    gpu_energy(const nrgprf::reader_gpu& rdr, const nrgprf::execution& exec, uint8_t dev, board_tag tag) :
        _valid(false),
        _energy{}
    {
        (void)tag;
        for (size_t s = 1; s < exec.size(); s++)
        {
            const nrgprf::sample& prev = exec.get(s - 1);
            const nrgprf::sample& curr = exec.get(s);

            nrgprf::result<nrgprf::units_power> pwr_prev = rdr.get_board_power(prev, dev);
            nrgprf::result<nrgprf::units_power> pwr_curr = rdr.get_board_power(curr, dev);

            if (!pwr_prev || !pwr_curr)
                return;

            nrgprf::duration_t dur = curr - prev;
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
public:
    inline static struct pkg_tag : std::integral_constant<uint8_t, 0> {} package;
    inline static struct pp0_tag : std::integral_constant<uint8_t, 1> {} cores;
    inline static struct pp1_tag : std::integral_constant<uint8_t, 2> {} uncore;
    inline static struct dram_tag : std::integral_constant<uint8_t, 3> {} dram;

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
    cpu_energy(const nrgprf::reader_rapl& reader, const nrgprf::execution& exec, uint8_t skt, pkg_tag tag) :
        _energy(subtract(reader.get_pkg_energy(exec.last(), skt), reader.get_pkg_energy(exec.first(), skt)))
    {
        (void)tag;
    }

    cpu_energy(const nrgprf::reader_rapl& reader, const nrgprf::execution& exec, uint8_t skt, pp0_tag tag) :
        _energy(subtract(reader.get_pp0_energy(exec.last(), skt), reader.get_pp0_energy(exec.first(), skt)))
    {
        (void)tag;
    }

    cpu_energy(const nrgprf::reader_rapl& reader, const nrgprf::execution& exec, uint8_t skt, pp1_tag tag) :
        _energy(subtract(reader.get_pp1_energy(exec.last(), skt), reader.get_pp1_energy(exec.first(), skt)))
    {
        (void)tag;
    }

    cpu_energy(const nrgprf::reader_rapl& reader, const nrgprf::execution& exec, uint8_t skt, dram_tag tag) :
        _energy(subtract(reader.get_dram_energy(exec.last(), skt), reader.get_dram_energy(exec.first(), skt)))
    {
        (void)tag;
    }

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
    nrgprf::joules<double> _energy;

    void get_energy(const nrgprf::joules<double>& normal, std::chrono::duration<double> dur,
        const nrgprf::joules<double>& idle, std::chrono::duration<double> idle_dur)
    {
        nrgprf::joules<double> norm = idle * (dur.count() / idle_dur.count());
        if (normal <= norm)
            return;
        _energy = normal - norm;
    }

public:
    idle_delta(const cpu_energy& normal, const std::chrono::duration<double>& dur,
        const cpu_energy& idle, const std::chrono::duration<double>& idle_dur) :
        _energy{}
    {
        get_energy(normal.get(), dur, idle.get(), idle_dur);
    }

    idle_delta(const gpu_energy& normal, const std::chrono::duration<double>& dur,
        const gpu_energy& idle, const std::chrono::duration<double>& idle_dur) :
        _energy{}
    {
        get_energy(normal.get(), dur, idle.get(), idle_dur);
    }

    const nrgprf::joules<double> get() const
    {
        return _energy;
    }

    explicit operator bool() const
    {
        return _energy.count() > 0;
    }
};

std::ostream& operator<<(std::ostream& os, const idle_delta& id)
{
    if (id)
        os << "(" << id.get() << ")";
    else
        os << "(n/a)";
    return os;
}


template<typename T>
class rdr_task_pair
{
private:
    const T& _reader;
    const nrgprf::task& _task;
    const nrgprf::execution& _idle;

public:
    rdr_task_pair(const T& reader, const nrgprf::task& task, const nrgprf::execution& idle) :
        _reader(reader),
        _task(task),
        _idle(idle)
    {}

    const T& reader() const
    {
        return _reader;
    }

    const nrgprf::task& task() const
    {
        return _task;
    }

    const nrgprf::execution& idle_values() const
    {
        return _idle;
    }
};

std::ostream& operator<<(std::ostream& os, const rdr_task_pair<nrgprf::reader_gpu>& rt)
{
    using namespace nrgprf;

    size_t padding = get_padding(get_num_digits(rt.task().size()));
    size_t num_execs = rt.task().size();
    bool has_idle_values = rt.idle_values().size() >= 2;

    std::chrono::duration<double> dur_sum{};
    joules<double> energy_sum[MAX_SOCKETS]{};
    joules<double> idle_sum[MAX_SOCKETS]{};
    for (size_t ix = 0; ix < num_execs; ix++)
    {
        const execution& exec = rt.task().get(ix);
        std::chrono::duration<double> duration = get_duration(exec);
        dur_sum += duration;

        os << std::setw(padding) << ix << outer_sep << duration;
        for (uint8_t dev = 0; dev < MAX_SOCKETS; dev++)
        {
            gpu_energy total_energy(rt.reader(), exec, dev, gpu_energy::board);
            if (!total_energy)
                continue;

            energy_sum[dev] += total_energy.get();
            os << outer_sep << "device=" << +dev << inner_sep << "board=" << total_energy.get();
            if (has_idle_values)
            {
                idle_delta id(total_energy, duration,
                    gpu_energy(rt.reader(), rt.idle_values(), dev, gpu_energy::board),
                    get_duration(rt.idle_values()));
                idle_sum[dev] += id.get();
                os << " " << id;
            }
        }
        os << "\n";
    }
    if (num_execs > 1)
    {
        std::stringstream ssum;
        std::stringstream savg;
        ssum << std::setw(padding) << sum_str << outer_sep << dur_sum;
        savg << std::setw(padding) << avg_str << outer_sep << dur_sum / num_execs;
        for (uint8_t dev = 0; dev < MAX_SOCKETS; dev++)
        {
            if (energy_sum[dev].count() != 0)
            {
                ssum << outer_sep << "device=" << +dev << inner_sep << "board="
                    << energy_sum[dev];
                savg << outer_sep << "device=" << +dev << inner_sep << "board="
                    << energy_sum[dev] / num_execs;
                if (has_idle_values && idle_sum[dev].count() > 0)
                {
                    ssum << " (" << idle_sum[dev] << ")";
                    savg << " (" << idle_sum[dev] / num_execs << ")";
                }
            }
        }
        os << savg.rdbuf() << "\n";
        os << ssum.rdbuf() << "\n";
    }
    return os;
}

std::ostream& operator<<(std::ostream& os, const rdr_task_pair<nrgprf::reader_rapl>& rt)
{
    static constexpr const char* prefix[] =
    {
        "package=",
        "cores=",
        "uncore=",
        "dram="
    };

    using namespace nrgprf;

    size_t padding = get_padding(get_num_digits(rt.task().size()));
    size_t num_execs = rt.task().size();

    joules<double> energy_sum[MAX_SOCKETS][MAX_RAPL_DOMAINS]{};
    joules<double> idle_sum[MAX_SOCKETS][MAX_RAPL_DOMAINS]{};
    std::chrono::duration<double> dur_sum{};
    for (size_t ix = 0; ix < num_execs; ix++)
    {
        const execution& exec = rt.task().get(ix);
        std::chrono::duration<double> duration = get_duration(exec);
        dur_sum += duration;

        os << std::setw(padding) << ix << outer_sep << duration;
        for (uint8_t skt = 0; skt < MAX_SOCKETS; skt++)
        {
            if (!socket_has_events(rt.reader(), skt))
                continue;
            os << outer_sep << "socket=" << +skt;

            if (rt.reader().event_idx(rapl_domain::PKG, skt) != -1)
            {
                cpu_energy::pkg_tag& tag = cpu_energy::package;
                cpu_energy pkg(rt.reader(), exec, skt, tag);
                energy_sum[skt][tag.value] += pkg.get();
                os << inner_sep << prefix[tag.value] << pkg.get();
                if (rt.idle_values().size() >= 2)
                {
                    idle_delta id(pkg, duration,
                        cpu_energy(rt.reader(), rt.idle_values(), skt, tag),
                        get_duration(rt.idle_values()));
                    idle_sum[skt][tag.value] += id.get();
                    os << " " << id;
                }
            }
            if (rt.reader().event_idx(rapl_domain::PP0, skt) != -1)
            {
                cpu_energy::pp0_tag& tag = cpu_energy::cores;
                cpu_energy pp0(rt.reader(), exec, skt, tag);
                energy_sum[skt][tag.value] += pp0.get();
                os << inner_sep << prefix[tag.value] << pp0.get();
                if (rt.idle_values().size() >= 2)
                {
                    idle_delta id(pp0, duration,
                        cpu_energy(rt.reader(), rt.idle_values(), skt, tag),
                        get_duration(rt.idle_values()));
                    idle_sum[skt][tag.value] += id.get();
                    os << " " << id;
                }
            }
            if (rt.reader().event_idx(rapl_domain::PP1, skt) != -1)
            {
                cpu_energy::pp1_tag& tag = cpu_energy::uncore;
                cpu_energy pp1(rt.reader(), exec, skt, tag);
                energy_sum[skt][tag.value] += pp1.get();
                os << inner_sep << prefix[tag.value] << pp1.get();
                if (rt.idle_values().size() >= 2)
                {
                    idle_delta id(pp1, duration,
                        cpu_energy(rt.reader(), rt.idle_values(), skt, tag),
                        get_duration(rt.idle_values()));
                    idle_sum[skt][tag.value] += id.get();
                    os << " " << id;
                }
            }
            if (rt.reader().event_idx(rapl_domain::DRAM, skt) != -1)
            {
                cpu_energy::dram_tag& tag = cpu_energy::dram;
                cpu_energy dram(rt.reader(), exec, skt, tag);
                energy_sum[skt][tag.value] += dram.get();
                os << inner_sep << prefix[tag.value] << dram.get();
                if (rt.idle_values().size() >= 2)
                {
                    idle_delta id(dram, duration,
                        cpu_energy(rt.reader(), rt.idle_values(), skt, tag),
                        get_duration(rt.idle_values()));
                    idle_sum[skt][tag.value] += id.get();
                    os << " " << id;
                }
            }
        }
        os << "\n";
    }
    if (num_execs > 1)
    {
        std::stringstream ssum;
        std::stringstream savg;
        ssum << std::setw(padding) << sum_str << outer_sep << dur_sum;
        savg << std::setw(padding) << avg_str << outer_sep << dur_sum / num_execs;
        for (uint8_t skt = 0; skt < MAX_SOCKETS; skt++)
        {
            if (!socket_has_events(rt.reader(), skt))
                continue;
            ssum << outer_sep << "socket=" << +skt;
            savg << outer_sep << "socket=" << +skt;
            for (uint8_t dmn = 0; dmn < MAX_RAPL_DOMAINS; dmn++)
            {
                if (energy_sum[skt][dmn].count() != 0)
                {
                    ssum << inner_sep << prefix[dmn] << energy_sum[skt][dmn];
                    savg << inner_sep << prefix[dmn] << energy_sum[skt][dmn] / num_execs;
                    if (rt.idle_values().size() >= 2 && idle_sum[skt][dmn].count() > 0)
                    {
                        ssum << " (" << idle_sum[skt][dmn] << ")";
                        savg << " (" << idle_sum[skt][dmn] / num_execs << ")";
                    }
                }
            }
        }
        os << savg.rdbuf() << "\n";
        os << ssum.rdbuf() << "\n";
    }
    return os;
}


tep::idle_results::idle_results() :
    cpu_readings(0),
    gpu_readings(1)
{}

tep::idle_results::idle_results(nrgprf::execution&& cpur, nrgprf::execution&& gpur) :
    cpu_readings(std::move(cpur)),
    gpu_readings(std::move(gpur))
{}

tep::section_results::section_results(const config_data::section& sec) :
    section(sec),
    readings(0)
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
