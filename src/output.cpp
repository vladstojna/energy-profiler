// output.cpp

#include "output.hpp"
#include "trap.hpp"

#include <cassert>
#include <iostream>
#include <iomanip>

#include <nlohmann/json.hpp>

using namespace tep;


namespace nlohmann
{
    template<typename T>
    struct adl_serializer<std::chrono::duration<T>>
    {
        static void to_json(json& j, const std::chrono::duration<T>& d)
        {
            j = { { "value", d.count() }, { "units", "seconds" } };
        }
    };
}


namespace tep::detail
{
    class output_impl
    {
    private:
        nlohmann::json* _json;

    public:
        output_impl(nlohmann::json& json) :
            _json(&json)
        {}

        nlohmann::json& json()
        {
            assert(_json != nullptr);
            return *_json;
        }

        const nlohmann::json& json() const
        {
            assert(_json != nullptr);
            return *_json;
        }
    };
}


namespace tep
{

    using energy_duration_pair = std::pair<nrgprf::joules<double>, std::chrono::duration<double>>;

    struct energy_holder
    {
        nrgprf::joules<double> total;
        nrgprf::joules<double> delta;
    };

    template<typename T>
    static std::string to_string(const T& obj)
    {
        std::ostringstream oss;
        oss << obj;
        return oss.str();
    }

    static std::chrono::duration<double> get_duration(const timed_execution& exec)
    {
        return exec.back() - exec.front();
    }

    static nrgprf::joules<double> get_idle_delta(energy_duration_pair normal, energy_duration_pair idle)
    {
        nrgprf::joules<double> norm = idle.first * (normal.second / idle.second);
        if (normal.first <= norm)
            return nrgprf::joules<double>{};
        return normal.first - norm;
    }

    static void to_json(nlohmann::json& j, const position_interval& interval)
    {
        j = { { "start", to_string(interval.start()) }, { "end", to_string(interval.end()) } };
    };

    static void to_json(nlohmann::json& j, const energy_holder& energy)
    {
        if (energy.total.count())
            j["energy"]["total"] = energy.total.count();
        else
            j["energy"]["total"] = nullptr;

        if (energy.delta.count())
            j["energy"]["delta"] = energy.delta.count();
        else
            j["energy"]["delta"] = nullptr;

        j["units"] = "joules";
    }

    static void to_json(nlohmann::json& j, const section_output& so)
    {
        using json = nlohmann::json;

        if (so.label().empty())
            j["label"] = nullptr;
        else
            j["label"] = so.label();

        if (so.extra().empty())
            j["extra"] = nullptr;
        else
            j["extra"] = so.extra();

        json& execs = j["executions"];
        for (const auto& pe : so.executions())
        {
            json exec;
            exec["range"] = *pe.interval;
            exec["time"] = get_duration(pe.exec);

            detail::output_impl impl(exec["readings"]);
            so.readings_out().output(impl, pe.exec);

            execs.push_back(std::move(exec));
        }
    }

    static void to_json(nlohmann::json& j, const group_output& go)
    {
        if (go.label().empty())
            j["label"] = nullptr;
        else
            j["label"] = go.label();
        for (const auto& so : go.sections())
            j["sections"].emplace_back(so);
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
    nrgprf::units_energy _energy;

    nrgprf::units_energy subtract(
        nrgprf::result<nrgprf::units_energy> last,
        nrgprf::result<nrgprf::units_energy> first)
    {
        if (!first)
            return nrgprf::units_energy{};
        if (!last)
            return nrgprf::units_energy{};
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
        return _energy;
    }

    explicit operator bool() const
    {
        return _energy.count();
    }

};



void readings_output_holder::push_back(std::unique_ptr<readings_output>&& outputs)
{
    _outputs.push_back(std::move(outputs));
}

void readings_output_holder::output(detail::output_impl& os, const timed_execution& exec) const
{
    for (const auto& out : _outputs)
        out->output(os, exec);
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
void readings_output_dev<nrgprf::reader_rapl>::output(detail::output_impl& os,
    const timed_execution& exec) const
{
    using reader_rapl = nrgprf::reader_rapl;
    using json = nlohmann::json;

    assert(exec.size() > 1);
    assert(_idle.empty() || _idle.size() > 1);

    bool has_idle = !_idle.empty() && _idle.size() > 1;
    std::chrono::duration<double> duration = get_duration(exec);
    for (uint32_t skt = 0; skt < nrgprf::max_sockets; skt++)
    {
        cpu_energy pkg(_reader, exec, skt, reader_rapl::package{});
        cpu_energy pp0(_reader, exec, skt, reader_rapl::cores{});
        cpu_energy pp1(_reader, exec, skt, reader_rapl::uncore{});
        cpu_energy dram(_reader, exec, skt, reader_rapl::dram{});
        if (!pkg && !pp0 && !pp1 && !dram)
            continue;

        json readings;
        readings["target"] = "cpu";
        readings["socket"] = skt;

        if (pkg)
        {
            if (has_idle)
                readings["package"] = energy_holder{ pkg.get(), get_idle_delta(
                    { pkg.get(), duration },
                    { cpu_energy(_reader, _idle, skt, reader_rapl::package{}).get(),
                        get_duration(_idle) }
                ) };
            else
                readings["package"] = energy_holder{ pkg.get(), {} };
        }
        else
            readings["package"] = nullptr;

        if (pp0)
        {
            if (has_idle)
                readings["cores"] = energy_holder{ pp0.get(), get_idle_delta(
                    { pp0.get(), duration },
                    { cpu_energy(_reader, _idle, skt, reader_rapl::cores{}).get(),
                        get_duration(_idle) }
                ) };
            else
                readings["cores"] = energy_holder{ pp0.get(), {} };
        }
        else
            readings["cores"] = nullptr;

        if (pp1)
        {
            if (has_idle)
                readings["uncore"] = energy_holder{ pp1.get(), get_idle_delta(
                    { pp1.get(), duration },
                    { cpu_energy(_reader, _idle, skt, reader_rapl::uncore{}).get(),
                        get_duration(_idle) }
                ) };
            else
                readings["uncore"] = energy_holder{ pp1.get(), {} };
        }
        else
            readings["uncore"] = nullptr;

        if (dram)
        {
            if (has_idle)
                readings["dram"] = energy_holder{ dram.get(), get_idle_delta(
                    { dram.get(), duration },
                    { cpu_energy(_reader, _idle, skt, reader_rapl::dram{}).get(),
                        get_duration(_idle) }
                ) };
            else
                readings["dram"] = energy_holder{ dram.get(), {} };
        }
        else
            readings["dram"] = nullptr;

        os.json().push_back(std::move(readings));
    }
}


template<>
void readings_output_dev<nrgprf::reader_gpu>::output(detail::output_impl& os,
    const timed_execution& exec) const
{
    using json = nlohmann::json;

    assert(exec.size() > 1);
    assert(_idle.empty() || _idle.size() > 1);

    bool has_idle = !_idle.empty() && _idle.size() > 1;
    std::chrono::duration<double> duration = get_duration(exec);
    for (uint32_t dev = 0; dev < nrgprf::max_devices; dev++)
    {
        gpu_energy total_energy(_reader, exec, dev, gpu_energy::board);
        if (!total_energy)
            continue;

        json readings;
        readings["target"] = "gpu";
        readings["device"] = dev;
        if (has_idle)
            readings["board"] = energy_holder{ total_energy.get(), get_idle_delta(
                { total_energy.get(), duration },
                { gpu_energy(_reader, _idle, dev, gpu_energy::board).get(), get_duration(_idle) }
            ) };
        else
            readings["board"] = energy_holder{ total_energy.get(), {} };

        os.json().push_back(std::move(readings));
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
    using json = nlohmann::json;
    json j;
    for (const auto& g : pr.groups())
        j.push_back(json{ { "group", g } });
    os << std::setw(4) << j;
    return os;
}
