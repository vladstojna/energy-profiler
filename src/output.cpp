// output.cpp

#include "output.hpp"
#include "trap.hpp"

#include <cassert>
#include <iostream>
#include <iomanip>

#include <nlohmann/json.hpp>

using namespace tep;

namespace
{
    template<typename T>
    std::string to_string(const T& obj)
    {
        std::ostringstream oss;
        oss << obj;
        return oss.str();
    }

    void units_output(nlohmann::json& j)
    {
        j["time"] = "ns";
        j["energy"] = "J";
        j["power"] = "W";
    }

#if defined NRG_X86_64
    void format_output(nlohmann::json& j)
    {
        j["cpu"].push_back("energy");
        j["gpu"].push_back("power");
    }
#elif defined NRG_PPC64
    void format_output(nlohmann::json& j)
    {
        j["cpu"].push_back("sensor_time");
        j["cpu"].push_back("power");
        j["gpu"].push_back("power");
    }
#endif // defined NRG_X86_64
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

namespace nlohmann
{
    template<>
    struct adl_serializer<nrgprf::timed_sample>
    {
        static void to_json(json& j, const nrgprf::timed_sample& sample)
        {
            j = std::chrono::duration_cast<std::chrono::nanoseconds>(
                sample.timepoint().time_since_epoch())
                .count();
        }
    };

#if defined NRG_X86_64
    template<>
    struct adl_serializer<nrgprf::sensor_value>
    {
        static void to_json(json& j, const nrgprf::sensor_value& sensor_value)
        {
            nlohmann::json values;
            values.push_back(nrgprf::unit_cast<nrgprf::joules<double>>(sensor_value).count());
            j = std::move(values);
        }
    };
#elif defined NRG_PPC64
    template<>
    struct adl_serializer<nrgprf::sensor_value>
    {
        static void to_json(json& j, const nrgprf::sensor_value& sensor_value)
        {
            nlohmann::json values;
            values.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(
                sensor_value.timestamp.time_since_epoch()).count());
            values.push_back(nrgprf::unit_cast<nrgprf::watts<double>>(sensor_value.power).count());
            j = std::move(values);
        }
    };
#endif // defined NRG_X86_64
}

namespace tep
{
    static void to_json(nlohmann::json& j, const position_interval& interval)
    {
        j = { { "start", to_string(interval.start()) }, { "end", to_string(interval.end()) } };
    }

    static void to_json(nlohmann::json& j, const idle_output& io)
    {
        if (!io.exec().empty())
        {
            j["sample_times"] = io.exec();
            detail::output_impl impl(j);
            io.readings_out().output(impl, io.exec());
        }
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
            exec["sample_times"] = pe.exec;
            detail::output_impl impl(exec);
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

    static void to_json(nlohmann::json& j, const profiling_results& pr)
    {
        units_output(j["units"]);
        format_output(j["format"]);
        j["idle"] = pr.idle();
        j["groups"] = pr.groups();
    }
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
readings_output_dev<Reader>::readings_output_dev(const Reader& r) :
    _reader(r)
{}

template<>
void readings_output_dev<nrgprf::reader_rapl>::output(detail::output_impl& os,
    const timed_execution& exec) const
{
    assert(exec.size() > 1);
    using namespace nrgprf;
    using json = nlohmann::json;

    os.json()["cpu"] = json::array();
    for (uint32_t skt = 0; skt < nrgprf::max_sockets; skt++)
    {
        json readings;
        readings["socket"] = skt;
        json& jpkg = readings["package"] = json::array();
        json& jcores = readings["cores"] = json::array();
        json& juncore = readings["uncore"] = json::array();
        json& jdram = readings["dram"] = json::array();
        json& jgpu = readings["gpu"] = json::array();
        json& jsys = readings["sys"] = json::array();

        for (const auto& sample : exec)
        {
            if (result<sensor_value> sens_value = _reader.value<loc::pkg>(sample, skt))
                jpkg.push_back(sens_value.value());
            if (result<sensor_value> sens_value = _reader.value<loc::cores>(sample, skt))
                jcores.push_back(sens_value.value());
            if (result<sensor_value> sens_value = _reader.value<loc::uncore>(sample, skt))
                juncore.push_back(sens_value.value());
            if (result<sensor_value> sens_value = _reader.value<loc::mem>(sample, skt))
                jdram.push_back(sens_value.value());
            if (result<sensor_value> sens_value = _reader.value<loc::sys>(sample, skt))
                jsys.push_back(sens_value.value());
            if (result<sensor_value> sens_value = _reader.value<loc::gpu>(sample, skt))
                jgpu.push_back(sens_value.value());
        }
        if (!jpkg.empty() || !jcores.empty() || !juncore.empty()
            || !jdram.empty() || !jgpu.empty() || !jsys.empty())
        {
            os.json()["cpu"].push_back(std::move(readings));
        }
    }
}

template<>
void readings_output_dev<nrgprf::reader_gpu>::output(detail::output_impl& os,
    const timed_execution& exec) const
{
    assert(exec.size() > 1);
    using namespace nrgprf;
    using json = nlohmann::json;

    os.json()["gpu"] = json::array();
    for (uint32_t dev = 0; dev < nrgprf::max_devices; dev++)
    {
        json readings;
        readings["device"] = dev;
        readings["board"] = json::array();
        for (const auto& sample : exec)
        {
            if (result<units_power> power = _reader.get_board_power(sample, dev))
            {
                json values;
                values.push_back(unit_cast<watts<double>>(power.value()).count());
                readings["board"].push_back(std::move(values));
            }
        }
        if (!readings["board"].empty())
            os.json()["gpu"].push_back(std::move(readings));
    }
}

idle_output::idle_output(std::unique_ptr<readings_output>&& rout, timed_execution&& exec) :
    _rout(std::move(rout)),
    _exec(std::move(exec))
{}

timed_execution& idle_output::exec()
{
    return _exec;
}

const timed_execution& idle_output::exec() const
{
    return _exec;
}

const readings_output& idle_output::readings_out() const
{
    assert(_rout);
    return *_rout;
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

std::vector<idle_output>& profiling_results::idle()
{
    return _idle;
}

const std::vector<idle_output>& profiling_results::idle() const
{
    return _idle;
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
    os << nlohmann::json(pr);
    return os;
}
