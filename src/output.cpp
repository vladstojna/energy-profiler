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

    static nrgprf::joules<double> get_idle_delta(
        const energy_duration_pair& normal,
        const energy_duration_pair& idle)
    {
        nrgprf::joules<double> norm = idle.first * (normal.second / idle.second);
        if (normal.first <= norm)
            return nrgprf::joules<double>{};
        return normal.first - norm;
    }

    static void to_json(nlohmann::json& j, const position_interval& interval)
    {
        j = { { "start", to_string(interval.start()) }, { "end", to_string(interval.end()) } };
    }

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

        j["energy"]["units"] = "joules";
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
        if (go.extra().empty())
            j["extra"] = nullptr;
        else
            j["extra"] = go.extra();
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
            nrgprf::result<nrgprf::units_power> pwr_prev = rdr.get_board_power(prev, dev);
            nrgprf::result<nrgprf::units_power> pwr_curr = rdr.get_board_power(curr, dev);
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

#if defined NRG_X86_64

template<typename T>
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
    cpu_energy(const nrgprf::reader_rapl& reader, const timed_execution& exec,
        uint8_t skt) :
        _energy(subtract(
            reader.value<T>(exec.back(), skt),
            reader.value<T>(exec.front(), skt)))
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

#elif defined NRG_PPC64

template<typename T>
class cpu_energy
{
private:
    nrgprf::joules<double> _energy;

    nrgprf::joules<double> compute(
        const nrgprf::reader_rapl& reader,
        const timed_execution& exec,
        uint8_t skt)
    {
        if (exec.empty())
            return {};
        nrgprf::joules<double> energy{};
        for (auto it = std::next(exec.begin()); it != exec.end(); it++)
        {
            const auto& sprev = *std::prev(it);
            const auto& scurr = *it;
            nrgprf::result<nrgprf::sensor_value> prev = reader.value<T>(sprev, skt);
            nrgprf::result<nrgprf::sensor_value> curr = reader.value<T>(scurr, skt);

            if (!prev || !curr)
                return {};
            nrgprf::time_point::duration interval =
                curr.value().timestamp - prev.value().timestamp;

            assert(interval.count() >= 0);
            if (interval.count() > 0)
            {
                nrgprf::watts<double> avg_pwr = (prev.value().power + curr.value().power) / 2.0;
                energy += (avg_pwr * interval);
            }
        }
        return energy;
    }

public:
    cpu_energy(const nrgprf::reader_rapl& reader,
        const timed_execution& exec,
        uint8_t skt) :
        _energy(compute(reader, exec, skt))
    {}

    const nrgprf::joules<double>& get() const
    {
        return _energy;
    }

    explicit operator bool() const
    {
        return _energy.count();
    }
};

#endif // defined NRG_X86_64

template<typename Tag>
void output_energy(nlohmann::json& j,
    const nrgprf::reader_rapl& reader,
    nrgprf::joules<double> energy_total,
    const timed_execution& exec,
    const timed_execution& idle,
    uint32_t skt)
{
    bool has_idle = !idle.empty() && idle.size() > 1;
    if (has_idle)
        j = energy_holder{ energy_total,
            get_idle_delta(
                { energy_total, get_duration(exec) },
                { cpu_energy<Tag>(reader, idle, skt).get(), get_duration(idle) }) };
    else
        j = energy_holder{ energy_total, {} };
}



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
class tep::readings_output_dev<nrgprf::reader_rapl>;

template
class tep::readings_output_dev<nrgprf::reader_gpu>;

template<typename Reader>
readings_output_dev<Reader>::readings_output_dev(const Reader& r, const timed_execution& idle) :
    _reader(r),
    _idle(idle)
{}


template<>
void readings_output_dev<nrgprf::reader_rapl>::output(detail::output_impl& os,
    const timed_execution& exec) const
{
    using json = nlohmann::json;

    assert(exec.size() > 1);
    assert(_idle.empty() || _idle.size() > 1);
    for (uint32_t skt = 0; skt < nrgprf::max_sockets; skt++)
    {
        cpu_energy<nrgprf::loc::pkg> pkg(_reader, exec, skt);
        cpu_energy<nrgprf::loc::cores> pp0(_reader, exec, skt);
        cpu_energy<nrgprf::loc::uncore> pp1(_reader, exec, skt);
        cpu_energy<nrgprf::loc::mem> dram(_reader, exec, skt);
        if (!pkg && !pp0 && !pp1 && !dram)
            continue;

        json readings;
        json& jpackage = readings["package"];
        json& jcores = readings["cores"];
        json& juncore = readings["uncore"];
        json& jdram = readings["dram"];

        readings["target"] = "cpu";
        readings["socket"] = skt;

        if (pkg)
            output_energy<nrgprf::loc::pkg>(jpackage, _reader, pkg.get(), exec, _idle, skt);
        if (pp0)
            output_energy<nrgprf::loc::cores>(jcores, _reader, pp0.get(), exec, _idle, skt);
        if (pp1)
            output_energy<nrgprf::loc::uncore>(juncore, _reader, pp1.get(), exec, _idle, skt);
        if (dram)
            output_energy<nrgprf::loc::mem>(jdram, _reader, dram.get(), exec, _idle, skt);

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
        json& jboard = readings["board"];

        readings["target"] = "gpu";
        readings["device"] = dev;
        if (has_idle)
            jboard = energy_holder{ total_energy.get(), get_idle_delta(
                { total_energy.get(), duration },
                { gpu_energy(_reader, _idle, dev, gpu_energy::board).get(), get_duration(_idle) }
            ) };
        else
            jboard = energy_holder{ total_energy.get(), {} };

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



group_output::group_output(std::string_view label, std::string_view extra) :
    _label(label),
    _extra(extra)
{}

section_output& group_output::push_back(section_output&& so)
{
    return _sections.emplace_back(std::move(so));
}

const std::string& group_output::label() const
{
    return _label;
}

const std::string& group_output::extra() const
{
    return _extra;
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
