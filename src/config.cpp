// config.cpp

#include "config.hpp"

#include <cassert>
#include <cstring>
#include <iostream>

#include <pugixml.hpp>
#include <expected.hpp>

using namespace tep;

namespace defaults
{
    static uint32_t domain_mask = ~0x0;
    static uint32_t socket_mask = ~0x0;
    static uint32_t device_mask = ~0x0;

    static config_data::target target = config_data::target::cpu;
    static config_data::profiling_method method = config_data::profiling_method::energy_total;
    static std::chrono::milliseconds interval(10);
    static std::chrono::milliseconds max_interval(30000);

    static uint32_t executions = 1;
    static uint32_t samples = 384;
}

template<typename R>
using cfg_expected = cmmn::expected<R, cfg_error>;

static const char* error_messages[] =
{
    "No error",

    "I/O error when loading config file",
    "Config file not found",
    "Out of memory when loading config file",
    "Config file is badly formatted",
    "Node <config></config> not found",

    "Invalid thread count in <threads></threads>",

    "Section list <sections></sections> is empty",
    "section: Node <bounds></bounds> not found",
    "section: Node <freq></freq> not found",
    "section: target must be 'cpu' or 'gpu'",
    "section: name cannot be empty",
    "section: extra data cannot be empty",
    "section: frequency must be a positive decimal number",
    "section: interval must be a positive integer",
    "section: method must be 'profile' or 'total'",
    "section: executions must be a positive integer",
    "section: samples must be a positive integer",
    "section: duration must be a positive integer",

    "params: parameter 'domain_mask' must be a valid integer",
    "params: parameter 'socket_mask' must be a valid integer",
    "params: parameter 'device_mask' must be a valid integer",

    "bounds: node <start></start> not found",
    "bounds: node <end></end> not found",
    "bounds: cannot be empty: must contain either <func/> or <start/> and <end/>",
    "bounds: too many nodes: must contain either <func/> or <start/> and <end/>",

    "start/end: node <cu></cu> or attribute 'cu' not found",
    "start/end: node <line></line> or attribute 'line' not found",
    "start/end: invalid compilation unit: cannot be empty",
    "start/end: invalid line number: must be a positive integer",

    "func: invalid compilation unit: cannot be empty",
    "func: attribute 'name' not found",
    "func: invalid name: cannot be empty"
};


// begin helper functions

cfg_expected<config_data::target> get_target(const pugi::xml_node& nsection)
{
    using namespace pugi;
    xml_attribute tgt_attr = nsection.attribute("target");
    if (!tgt_attr)
        return defaults::target;
    const char_t* tgt_str = tgt_attr.value();
    if (!strcmp(tgt_str, "cpu"))
        return config_data::target::cpu;
    if (!strcmp(tgt_str, "gpu"))
        return config_data::target::gpu;
    return cfg_error(cfg_error_code::SEC_INVALID_TARGET);
}

cfg_expected<config_data::params> get_params(const pugi::xml_node& nparams)
{
    using namespace pugi;
    // all domains, devices and sockets are considered by default
    unsigned int dommask = defaults::domain_mask;
    unsigned int sktmask = defaults::socket_mask;
    unsigned int devmask = defaults::device_mask;
    xml_node ndomains = nparams.child("domain_mask");
    // <domain_mask></domain_mask> exists
    if (ndomains)
        // <domain_mask></domain_mask> must be a valid, positive integer
        if (!(dommask = ndomains.text().as_uint(0)))
            return cfg_error(cfg_error_code::PARAM_INVALID_DOMAIN_MASK);
    xml_node nsockets = nparams.child("socket_mask");
    // <socket_mask></socket_mask> exists
    if (nsockets)
        // <socket_mask></socket_mask> must be a valid, positive integer
        if (!(sktmask = nsockets.text().as_uint(0)))
            return cfg_error(cfg_error_code::PARAM_INVALID_SOCKET_MASK);
    xml_node ndevs = nparams.child("device_mask");
    // <device_mask></device_mask> exists
    if (ndevs)
        // <device_mask></device_mask> must be a valid, positive integer
        if (!(devmask = ndevs.text().as_uint(0)))
            return cfg_error(cfg_error_code::PARAM_INVALID_DEVICE_MASK);
    return { dommask, sktmask, devmask };
}

cfg_expected<std::chrono::milliseconds> get_interval(const pugi::xml_node& nsection,
    config_data::profiling_method method)
{
    using namespace pugi;

    std::chrono::milliseconds eint = defaults::interval;
    xml_node nfreq = nsection.child("freq");
    xml_node nint = nsection.child("interval");
    // <interval></interval> overrides <freq></freq>
    if (nint)
    {
        // <interval></interval> must be a valid, positive integer
        int interval = nint.text().as_int(0);
        if (interval <= 0)
            return cfg_error(cfg_error_code::SEC_INVALID_INTERVAL);
        eint = std::chrono::milliseconds(interval);
    }
    else if (nfreq)
    {
        // <freq></freq> must be a positive decimal number
        double freq = nfreq.text().as_double(0.0);
        if (freq <= 0.0)
            return cfg_error(cfg_error_code::SEC_INVALID_FREQ);
        // clamps at 1000 Hz
        double interval = 1000.0 / freq;
        eint = std::chrono::milliseconds(interval <= 1.0 ? 1 : static_cast<unsigned int>(interval));
    }
    if (method == config_data::profiling_method::energy_total)
        eint = defaults::max_interval;
    return eint;
}

cfg_expected<uint32_t> get_samples(const pugi::xml_node& nsection,
    const std::chrono::milliseconds& interval)
{
    using namespace pugi;
    xml_node nsamp = nsection.child("samples");
    xml_node ndur = nsection.child("duration");
    if (ndur)
    {
        int duration = ndur.text().as_int(0);
        if (duration <= 0)
            return cfg_error(cfg_error_code::SEC_INVALID_DURATION);
        return duration / interval.count() + (duration % interval.count() != 0);
    }
    if (nsamp)
    {
        int samples = nsamp.text().as_int(0);
        if (samples <= 0)
            return cfg_error(cfg_error_code::SEC_INVALID_SAMPLES);
        return samples;
    }
    return defaults::samples;
}

cfg_expected<uint32_t> get_execs(const pugi::xml_node& nsection)
{
    using namespace pugi;
    // <execs></execs> - optional, must be a positive integer
    // if not present - use default value
    xml_node nexecs = nsection.child("execs");
    int execs = 0;
    if (nexecs && (execs = nexecs.text().as_int(0)) <= 0)
        return cfg_error(cfg_error_code::SEC_INVALID_EXECS);
    return execs ? static_cast<uint32_t>(execs) : defaults::executions;
}

cfg_expected<std::string> get_cu(const pugi::xml_node& pos_node)
{
    using namespace pugi;
    // attribute "cu" exists
    xml_attribute cu_attr = pos_node.attribute("cu");
    if (cu_attr)
    {
        // if attribute exists - it cannot be empty
        if (!*cu_attr.value())
            return cfg_error(cfg_error_code::POS_INVALID_COMP_UNIT);
        return cu_attr.value();
    }
    // fallback to checking child node
    xml_node cu = pos_node.child("cu");
    // <cu></cu> exists
    if (!cu)
        return cfg_error(cfg_error_code::POS_NO_COMP_UNIT);
    // <cu></cu> is not empty
    if (!*cu.child_value())
        return cfg_error(cfg_error_code::POS_INVALID_COMP_UNIT);
    return cu.child_value();
}

cfg_expected<uint32_t> get_lineno(const pugi::xml_node& pos_node)
{
    using namespace pugi;
    // attribute "line" exists
    xml_attribute line_attr = pos_node.attribute("line");
    if (line_attr)
    {
        // if attribute exists - it cannot be empty
        if (!*line_attr.value())
            return cfg_error(cfg_error_code::POS_INVALID_LINE);
        int lineno;
        if ((lineno = line_attr.as_int(0)) <= 0)
            return cfg_error(cfg_error_code::POS_INVALID_LINE);
        return lineno;
    }
    // fallback to checking child node
    xml_node line = pos_node.child("line");
    // <line></line> exists
    if (!line)
        return cfg_error(cfg_error_code::POS_NO_LINE);
    // <line></line> is not empty or negative
    int lineno;
    if ((lineno = line.text().as_int(0)) <= 0)
        return cfg_error(cfg_error_code::POS_INVALID_LINE);
    return lineno;
}

cfg_expected<config_data::position> get_position(const pugi::xml_node& pos_node)
{
    using namespace pugi;
    cfg_expected<std::string> cu = get_cu(pos_node);
    if (!cu)
        return std::move(cu.error());
    cfg_expected<uint32_t> lineno = get_lineno(pos_node);
    if (!lineno)
        return std::move(lineno.error());
    return { std::move(cu.value()), lineno.value() };
}

cfg_expected<config_data::function> get_function(const pugi::xml_node& func_node)
{
    using namespace pugi;
    // attribute "cu" exists
    std::string cu;
    xml_attribute cu_attr = func_node.attribute("cu");
    if (cu_attr)
    {
        // if attribute exists - it cannot be empty
        if (!*cu_attr.value())
            return cfg_error(cfg_error_code::FUNC_INVALID_COMP_UNIT);
        cu.append(cu_attr.value());
    }
    xml_attribute name_attr = func_node.attribute("name");
    if (!name_attr)
        return cfg_error(cfg_error_code::FUNC_NO_NAME);
    if (!*name_attr.value())
        return cfg_error(cfg_error_code::FUNC_INVALID_NAME);
    return config_data::function(std::move(cu), name_attr.value());
}

cfg_expected<config_data::bounds> get_bounds(const pugi::xml_node& bounds)
{
    using namespace pugi;
    // <start/>
    xml_node nstart = bounds.child("start");
    // <end/>
    xml_node nend = bounds.child("end");
    // <func/>
    xml_node nfunc = bounds.child("func");

    if (nstart || nend)
    {
        if (nfunc)
            return cfg_error(cfg_error_code::BOUNDS_TOO_MANY);
        if (!nend)
            return cfg_error(cfg_error_code::BOUNDS_NO_END);
        if (!nstart)
            return cfg_error(cfg_error_code::BOUNDS_NO_START);

        cfg_expected<config_data::position> pstart = get_position(nstart);
        if (!pstart)
            return std::move(pstart.error());
        cfg_expected<config_data::position> pend = get_position(nend);
        if (!pend)
            return std::move(pend.error());

        return { std::move(pstart.value()), std::move(pend.value()) };
    }
    else if (nfunc)
    {
        assert(!nstart && !nend);
        cfg_expected<config_data::function> func = get_function(nfunc);
        if (!func)
            return std::move(func.error());
        return std::move(func.value());
    }
    return cfg_error(cfg_error_code::BOUNDS_EMPTY);
}

cfg_expected<config_data::profiling_method> get_method(const pugi::xml_node& nsection,
    config_data::target tgt)
{
    using namespace pugi;

    config_data::profiling_method method = defaults::method;
    // <method></method> - optional
    // no effect when target is 'gpu' due to the
    // nature of the power/energy reading interface
    xml_node nmethod = nsection.child("method");
    if (nmethod)
    {
        const pugi::char_t* method_str = nmethod.child_value();
        if (!strcmp(method_str, "profile"))
            method = config_data::profiling_method::energy_profile;
        else if (!strcmp(method_str, "total"))
            method = config_data::profiling_method::energy_total;
        else
            return cfg_error(cfg_error_code::SEC_INVALID_METHOD);
    }
    if (tgt == config_data::target::gpu)
        method = config_data::profiling_method::energy_profile;
    return method;
}

cfg_expected<config_data::section> get_section(const pugi::xml_node& nsection)
{
    using namespace pugi;
    // attribute target
    cfg_expected<config_data::target> target = get_target(nsection);
    if (!target)
        return std::move(target.error());

    // <name></name> - optional, must not be empty
    xml_node nname = nsection.child("name");
    if (nname && !*nname.child_value())
        return cfg_error(cfg_error_code::SEC_INVALID_NAME);
    // <extra></extra> - optional, must not be empty
    xml_node nxtra = nsection.child("extra");
    if (nxtra && !*nxtra.child_value())
        return cfg_error(cfg_error_code::SEC_INVALID_EXTRA);

    cfg_expected<config_data::profiling_method> method = get_method(nsection, target.value());
    if (!method)
        return std::move(method.error());

    cfg_expected<std::chrono::milliseconds> interval = get_interval(nsection, method.value());
    if (!interval)
        return std::move(interval.error());

    cfg_expected<uint32_t> execs = get_execs(nsection);
    if (!execs)
        return std::move(execs.error());

    cfg_expected<uint32_t> samples = get_samples(nsection, interval.value());
    if (!samples)
        return std::move(samples.error());

    // <bounds></bounds>
    xml_node nbounds = nsection.child("bounds");
    if (!nbounds)
        return cfg_error(cfg_error_code::SEC_NO_BOUNDS);
    cfg_expected<config_data::bounds> bounds = get_bounds(nbounds);
    if (!bounds)
        return std::move(bounds.error());

    return {
        nname.child_value(),
        nxtra.child_value(),
        target.value(),
        method.value(),
        std::move(bounds.value()),
        std::move(interval.value()),
        execs.value(),
        samples.value() };
}

// end helper functions


// position

config_data::position::position(const std::string& cu, uint32_t ln) :
    _cu(cu),
    _line(ln)
{}

config_data::position::position(std::string&& cu, uint32_t ln) :
    _cu(std::move(cu)),
    _line(ln)
{}

config_data::position::position(const char* cu, uint32_t ln) :
    _cu(cu),
    _line(ln)
{}

const std::string& config_data::position::compilation_unit() const
{
    return _cu;
}

uint32_t config_data::position::line() const
{
    return _line;
}


// function

template<typename C, typename N>
config_data::function::function(C&& cu, N&& name) :
    _cu(std::forward<C>(cu)),
    _name(std::forward<N>(name))
{}

template<typename N>
config_data::function::function(const char* cu, N&& name) :
    _cu(cu),
    _name(std::forward<N>(name))
{}

template<typename C>
config_data::function::function(C&& cu, const char* name) :
    _cu(std::forward<C>(cu)),
    _name(name)
{}

template
config_data::function::function(const std::string&, const std::string&);

template
config_data::function::function(const std::string&, std::string&&);

template
config_data::function::function(const std::string&, const char*);

template
config_data::function::function(std::string&&, const std::string&);

template
config_data::function::function(std::string&&, std::string&&);

template
config_data::function::function(std::string&&, const char*);

template
config_data::function::function(const char*, const std::string&);

template
config_data::function::function(const char*, std::string&&);

config_data::function::function(const char* cu, const char* name) :
    _cu(cu),
    _name(name)
{}

config_data::function::function(const std::string& name) :
    _cu(),
    _name(name)
{}

config_data::function::function(std::string&& name) :
    _cu(),
    _name(std::move(name))
{}

config_data::function::function(const char* name) :
    _cu(),
    _name(name)
{}

const std::string& config_data::function::cu() const
{
    return _cu;
}

const std::string& config_data::function::name() const
{
    return _name;
}

bool config_data::function::has_cu() const
{
    return !_cu.empty();
}


// bounds

enum class config_data::bounds::type
{
    position,
    function
};

void config_data::bounds::copy_data(const bounds& other)
{
    switch (_tag)
    {
    case type::position:
        new (&_positions.start) auto(other._positions.start);
        new (&_positions.end) auto(other._positions.end);
        break;
    case type::function:
        new (&_func) auto(other._func);
        break;
    default:
        assert(false);
    }
}

void config_data::bounds::move_data(bounds&& other)
{
    switch (_tag)
    {
    case type::position:
        new (&_positions.start) auto(std::move(other._positions.start));
        new (&_positions.end) auto(std::move(other._positions.end));
        break;
    case type::function:
        new (&_func) auto(std::move(other._func));
        break;
    default:
        assert(false);
    }
}

template<typename S, typename E>
config_data::bounds::bounds(S&& s, E&& e) :
    _tag(type::position),
    _positions{ std::forward<S>(s), std::forward<E>(e) }
{}

template
config_data::bounds::bounds(const config_data::position&, const config_data::position&);

template
config_data::bounds::bounds(const config_data::position&, config_data::position&&);

template
config_data::bounds::bounds(config_data::position&&, const config_data::position&);

template
config_data::bounds::bounds(config_data::position&&, config_data::position&&);

config_data::bounds::bounds(const function& func) :
    _tag(type::function),
    _func(func)
{}

config_data::bounds::bounds(function&& func) :
    _tag(type::function),
    _func(std::move(func))
{}

config_data::bounds::~bounds()
{
    switch (_tag)
    {
    case type::position:
        _positions.start.~position();
        _positions.end.~position();
        break;
    case type::function:
        _func.~function();
        break;
    default:
        assert(false);
    }
}

config_data::bounds::bounds(const bounds& other) :
    _tag(other._tag)
{
    copy_data(other);
}

config_data::bounds::bounds(bounds&& other) :
    _tag(std::move(other._tag))
{
    move_data(std::move(other));
}

config_data::bounds& config_data::bounds::operator=(const bounds& other)
{
    if (_tag != other._tag)
    {
        this->~bounds();
        _tag = other._tag;
    }
    copy_data(other);
    return *this;
}

config_data::bounds& config_data::bounds::operator=(bounds&& other)
{
    if (_tag != other._tag)
    {
        this->~bounds();
        _tag = std::move(other._tag);
    }
    move_data(std::move(other));
    return *this;
}

bool config_data::bounds::has_positions() const
{
    return _tag == type::position;
}

bool config_data::bounds::has_function() const
{
    return _tag == type::function;
}

const config_data::position& config_data::bounds::start() const
{
    assert(_tag == type::position);
    return _positions.start;
}

const config_data::position& config_data::bounds::end() const
{
    assert(_tag == type::position);
    return _positions.end;
}

const config_data::function& config_data::bounds::func() const
{
    assert(_tag == type::function);
    return _func;
}


// params

config_data::params::params() :
    params(defaults::domain_mask, defaults::socket_mask, defaults::device_mask)
{}

config_data::params::params(unsigned int dommask, unsigned int sktmask, unsigned int devmask) :
    _domain_mask(dommask),
    _socket_mask(sktmask),
    _device_mask(devmask)
{}

unsigned int config_data::params::domain_mask() const
{
    return _domain_mask;
}

unsigned int config_data::params::socket_mask() const
{
    return _socket_mask;
}

unsigned int config_data::params::device_mask() const
{
    return _device_mask;
}


// section

const std::string& config_data::section::name() const
{
    return _name;
}

const std::string& config_data::section::extra() const
{
    return _extra;
}

config_data::target config_data::section::target() const
{
    return _target;
}

config_data::profiling_method config_data::section::method() const
{
    return _method;
}

const config_data::bounds& config_data::section::bounds() const
{
    return _bounds;
}

const std::chrono::milliseconds& config_data::section::interval() const
{
    return _interval;
}

uint32_t config_data::section::executions() const
{
    return _executions;
}

uint32_t config_data::section::samples() const
{
    return _samples;
}

bool config_data::section::has_name() const
{
    return !_name.empty();
}

bool config_data::section::has_extra() const
{
    return !_extra.empty();
}


// config_data

uint32_t config_data::threads() const
{
    return _threads;
}

void config_data::threads(uint32_t t)
{
    _threads = t;
}

void config_data::parameters(const config_data::params& p)
{
    _parameters = p;
}

const config_data::params& config_data::parameters() const
{
    return _parameters;
}

std::vector<config_data::section>& config_data::sections()
{
    return _sections;
}

const std::vector<config_data::section>& config_data::sections() const
{
    return _sections;
}

bool config_data::has_section_with(config_data::target tgt) const
{
    for (const auto& sec : _sections)
        if (sec.target() == tgt)
            return true;
    return false;
}

bool config_data::has_section_with(config_data::profiling_method method) const
{
    for (const auto& sec : _sections)
        if (sec.method() == method)
            return true;
    return false;
}


// load_config

cfg_expected<config_data> tep::load_config(const char* file)
{
    assert(file != nullptr);
    using namespace pugi;

    config_data cfgdata;
    xml_document doc;
    xml_parse_result parse_result = doc.load_file(file);
    if (!parse_result)
    {
        switch (parse_result.status)
        {
        case status_file_not_found:
            return cfg_error(cfg_error_code::CONFIG_NOT_FOUND);
        case status_io_error:
            return cfg_error(cfg_error_code::CONFIG_IO_ERROR);
        case status_out_of_memory:
            return cfg_error(cfg_error_code::CONFIG_OUT_OF_MEM);
        default:
            return cfg_error(cfg_error_code::CONFIG_BAD_FORMAT);
        }
    }
    // <config></config>
    xml_node nconfig = doc.child("config");
    if (!nconfig)
        return cfg_error(cfg_error_code::CONFIG_NO_CONFIG);

    // <threads></threads> - optional, must be positive integer
    // default value of 0
    xml_node nthreads = nconfig.child("threads");
    int threads = 0;
    if (nthreads && (threads = nthreads.text().as_int(0)) <= 0)
        return cfg_error(cfg_error_code::INVALID_THREAD_CNT);

    // <params></params> - optional, use default values if does not exist
    xml_node nparams = nconfig.child("params");
    if (nparams)
    {
        cfg_expected<config_data::params> custom_params = get_params(nparams);
        if (!custom_params)
            return std::move(custom_params.error());
        cfgdata.parameters(custom_params.value());
    }

    // iterate all sections
    // <sections></sections> - optional
    xml_node nsections = nconfig.child("sections");
    if (nsections)
    {
        int sec_count = 0;
        for (xml_node nsection = nsections.first_child(); nsection;
            nsection = nsection.next_sibling(), sec_count++)
        {
            // <section></section>
            cfg_expected<config_data::section> section = get_section(nsection);
            if (!section)
                return std::move(section.error());
            cfgdata.sections().push_back(std::move(section.value()));
        }
        if (sec_count == 0)
            return cfg_error(cfg_error_code::SEC_LIST_EMPTY);
    }

    cfgdata.threads(threads);
    return cfgdata;
}

cfg_result tep::load_config(const std::string& file)
{
    return load_config(file.c_str());
}

// operator overloads

std::ostream& tep::operator<<(std::ostream& os, const cfg_error& res)
{
    auto idx = static_cast<std::underlying_type_t<cfg_error_code>>(res.code());
    os << error_messages[idx] << " (error code " << idx << ")";
    return os;
}

std::ostream& tep::operator<<(std::ostream& os, const config_data::target& tgt)
{
    switch (tgt)
    {
    case config_data::target::cpu:
        os << "cpu";
        break;
    case config_data::target::gpu:
        os << "gpu";
        break;
    }
    return os;
}

std::ostream& tep::operator<<(std::ostream& os, const config_data::params& p)
{
    std::ios::fmtflags flags(os.flags());
    os << "domains: " << "0x" << std::hex << p.domain_mask();
    os << "\nsockets: " << "0x" << p.socket_mask();
    os << "\ndevices: " << "0x" << p.device_mask();
    os.flags(flags);
    return os;
}

std::ostream& tep::operator<<(std::ostream& os, const config_data::profiling_method& pm)
{
    switch (pm)
    {
    case config_data::profiling_method::energy_profile:
        os << "profile";
        break;
    case config_data::profiling_method::energy_total:
        os << "total energy consumption";
        break;
    }
    return os;
}

std::ostream& tep::operator<<(std::ostream& os, const config_data::position& p)
{
    os << p.compilation_unit() << ":" << p.line();
    return os;
}

std::ostream& tep::operator<<(std::ostream& os, const config_data::function& f)
{
    if (f.has_cu())
        os << f.cu() << ":";
    os << f.name();
    return os;
}

std::ostream& tep::operator<<(std::ostream& os, const config_data::bounds& b)
{
    switch (b._tag)
    {
    case config_data::bounds::type::position:
        os << b.start() << " - " << b.end();
        break;
    case config_data::bounds::type::function:
        os << b.func();
        break;
    default:
        assert(false);
    }
    return os;
}

std::ostream& tep::operator<<(std::ostream& os, const config_data::section& s)
{
    os << "name: " << (s.has_name() ? s.name() : "-");
    os << "\nextra: " << (s.has_extra() ? s.extra() : "-");
    os << "\ntarget: " << s.target();
    os << "\ninterval: " << s.interval().count() << " ms";
    os << "\nmethod: " << s.method();
    os << "\nbounds: " << s.bounds();
    os << "\nexecutions: " << s.executions();
    os << "\nsamples: " << s.samples();
    return os;
}

std::ostream& tep::operator<<(std::ostream& os, const config_data& cd)
{
    os << "threads: " << cd.threads();
    os << "\n" << cd.parameters();
    os << "\nsections:";
    for (const auto& section : cd.sections())
        os << "\n----------\n" << section;
    return os;
}


bool tep::operator==(const config_data::params& lhs, const config_data::params& rhs)
{
    return lhs.domain_mask() == rhs.domain_mask() &&
        lhs.socket_mask() == rhs.socket_mask() &&
        lhs.device_mask() == rhs.device_mask();
}

bool tep::operator==(const config_data::position& lhs, const config_data::position& rhs)
{
    return lhs.compilation_unit() == rhs.compilation_unit() && lhs.line() == rhs.line();
}

bool tep::operator==(const config_data::function& lhs, const config_data::function& rhs)
{
    return lhs.cu() == rhs.cu() && lhs.name() == rhs.name();
}

bool tep::operator==(const config_data::bounds& lhs, const config_data::bounds& rhs)
{
    return lhs.start() == rhs.start() && lhs.end() == rhs.end();
}

bool tep::operator==(const config_data::section& lhs, const config_data::section& rhs)
{
    return lhs.name() == rhs.name() && lhs.extra() == rhs.extra() &&
        lhs.target() == rhs.target() && lhs.method() == rhs.method() &&
        lhs.bounds() == rhs.bounds() && lhs.interval() == rhs.interval() &&
        lhs.executions() == rhs.executions() && lhs.samples() == lhs.samples();
}
