// output.cpp

#include "output.hpp"
#include "trap.hpp"

#include <cassert>
#include <cmath>
#include <functional>
#include <iostream>
#include <iomanip>

using namespace tep;

static std::string group_indent;
static std::string section_indent(4, ' ');
static std::string exec_indent(8, ' ');
static std::string readings_indent(12, ' ');


namespace tep
{

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

    static std::ostream& operator<<(std::ostream& os, const section_output& so)
    {
        os << section_indent << "label=" << so.label() << "\n";
        os << section_indent << "extra=" << so.extra() << "\n";
        os << section_indent << "executions:\n";
        for (const auto& exec : so.executions())
        {
            os << exec_indent << *exec.interval << "\n";
            os << exec_indent << get_duration(exec.exec) << "\n";
            os << exec_indent << "readings:\n";
            so.readings_out().output(os, exec.exec);
            os << "\n";
        }
        return os;
    }

    static std::ostream& operator<<(std::ostream& os, const group_output& go)
    {
        os << group_indent << "group=" << go.label() << "\n";
        os << group_indent << "sections:\n";
        for (const auto& sec : go.sections())
            os << sec << "\n";
        return os;
    }

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



void readings_output_holder::push_back(std::unique_ptr<readings_output>&& outputs)
{
    _outputs.push_back(std::move(outputs));
}

void readings_output_holder::output(std::ostream& os, const timed_execution& exec) const
{
    for (const auto& out : _outputs)
    {
        out->output(os, exec);
        os << "\n";
    }
}



template
class readings_output_dev<nrgprf::reader_rapl>;

template
class readings_output_dev<nrgprf::reader_gpu>;

template<typename Reader>
readings_output_dev<Reader>::readings_output_dev(const Reader& r, const timed_execution& idle) :
    _reader(r),
    _idle(idle)
{}


template<>
void readings_output_dev<nrgprf::reader_rapl>::output(std::ostream& os,
    const timed_execution& exec) const
{
    os << readings_indent;
    os << "target=cpu";
    std::chrono::duration<double> duration = get_duration(exec);
    for (uint32_t skt = 0; skt < nrgprf::max_sockets; skt++)
    {
        cpu_energy pkg(_reader, exec, skt, nrgprf::reader_rapl::package{});
        cpu_energy pp0(_reader, exec, skt, nrgprf::reader_rapl::cores{});
        cpu_energy pp1(_reader, exec, skt, nrgprf::reader_rapl::uncore{});
        cpu_energy dram(_reader, exec, skt, nrgprf::reader_rapl::dram{});

        if (!pkg && !pp0 && !pp1 && !dram)
            continue;

        os << " | socket=" << skt;
        if (pkg)
        {
            os << ", package=" << pkg.get();
            if (_idle.size() >= 2)
                os << " " << idle_delta(pkg, duration,
                    cpu_energy(_reader, _idle, skt, nrgprf::reader_rapl::package{}),
                    get_duration(_idle));
        }
        if (pp0)
        {
            os << ", cores=" << pp0.get();
            if (_idle.size() >= 2)
                os << " " << idle_delta(pp0, duration,
                    cpu_energy(_reader, _idle, skt, nrgprf::reader_rapl::cores{}),
                    get_duration(_idle));
        }
        if (pp1)
        {
            os << ", uncore=" << pp1.get();
            if (_idle.size() >= 2)
                os << " " << idle_delta(pp1, duration,
                    cpu_energy(_reader, _idle, skt, nrgprf::reader_rapl::uncore{}),
                    get_duration(_idle));
        }
        if (dram)
        {
            os << ", dram=" << dram.get();
            if (_idle.size() >= 2)
                os << " " << idle_delta(dram, duration,
                    cpu_energy(_reader, _idle, skt, nrgprf::reader_rapl::dram{}),
                    get_duration(_idle));
        }
    }
}


template<>
void readings_output_dev<nrgprf::reader_gpu>::output(std::ostream& os,
    const timed_execution& exec) const
{
    os << readings_indent;
    os << "target=gpu";
    std::chrono::duration<double> duration = get_duration(exec);
    for (uint32_t dev = 0; dev < nrgprf::max_devices; dev++)
    {
        gpu_energy total_energy(_reader, exec, dev, gpu_energy::board);
        if (!total_energy)
            continue;

        os << " | device=" << dev << ", board=" << total_energy.get();
        if (_idle.size() >= 2)
        {
            os << " " << idle_delta(total_energy, duration,
                gpu_energy(_reader, _idle, dev, gpu_energy::board),
                get_duration(_idle));
        }
    }
}



section_output::section_output(std::unique_ptr<readings_output>&& rout,
    std::string_view label, std::string_view extra) :
    _rout(std::move(rout)),
    _label(label),
    _extra(extra)
{}

section_output::section_output(std::unique_ptr<readings_output>&& rout,
    std::string_view label, std::string&& extra) :
    _rout(std::move(rout)),
    _label(label),
    _extra(std::move(extra))
{}

section_output::section_output(std::unique_ptr<readings_output>&& rout,
    std::string&& label, std::string_view extra) :
    _rout(std::move(rout)),
    _label(std::move(label)),
    _extra(extra)
{}

section_output::section_output(std::unique_ptr<readings_output>&& rout,
    std::string&& label, std::string&& extra) :
    _rout(std::move(rout)),
    _label(std::move(label)),
    _extra(std::move(extra))
{}

position_exec& section_output::push_back(position_exec&& pe)
{
    return _executions.emplace_back(std::move(pe));
}

const readings_output& section_output::readings_out() const
{
    assert(_rout);
    return *_rout;
}

const std::string& section_output::label() const
{
    return _label;
}

const std::string& section_output::extra() const
{
    return _extra;
}

const std::vector<position_exec>& section_output::executions() const
{
    return _executions;
}



group_output::group_output(std::string_view label) :
    _label(label)
{}

group_output::group_output(std::string&& label) :
    _label(std::move(label))
{}

section_output& group_output::push_back(section_output&& so)
{
    return _sections.emplace_back(std::move(so));
}

const std::string& group_output::label() const
{
    return _label;
}

group_output::container& group_output::sections()
{
    return _sections;
}

const group_output::container& group_output::sections() const
{
    return _sections;
}



profiling_results::container& profiling_results::groups()
{
    return _results;
}

const profiling_results::container& profiling_results::groups() const
{
    return _results;
}


// operator overloads

std::ostream& tep::operator<<(std::ostream& os, const profiling_results& pr)
{
    for (const auto& g : pr.groups())
        os << g << "\n";
    return os;
}
