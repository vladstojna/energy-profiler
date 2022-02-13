// config.cpp

#include "config.hpp"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <iostream>
#include <iomanip>
#include <charconv>

#include <pugixml.hpp>
#include <nonstd/expected.hpp>

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
using cfg_expected = nonstd::expected<R, cfg_error>;

constexpr static const char* error_messages[] =
{
    "No error",

    "I/O error when loading config file",
    "Config file not found",
    "Out of memory when loading config file",
    "Config file is badly formatted",
    "Node <config></config> not found",

    "section: Node <bounds></bounds> not found",
    "section: Node <freq></freq> not found",
    "section: all targets must be 'cpu' or 'gpu', separated by a comma",
    "section: label cannot be empty",
    "section: extra data cannot be empty",
    "section: frequency must be a positive decimal number",
    "section: interval must be a positive integer",
    "section: method must be 'profile' or 'total'",
    "section: executions must be a positive integer",
    "section: samples must be a positive integer",
    "section: duration must be a positive integer",
    "section: section label already exists",
    "section: cannot have both <short/> and <long/> tags",
    "section: invalid <method></method> for <short/>",

    "section group: <sections></sections> is empty",
    "section group: label cannot be empty",
    "section group: group label already exists",
    "section group: extra data cannot be empty",

    "params: parameter 'domain_mask' must be a valid integer",
    "params: parameter 'socket_mask' must be a valid integer",
    "params: parameter 'device_mask' must be a valid integer",

    "bounds: node <start></start> not found",
    "bounds: node <end></end> not found",
    "bounds: cannot be empty: must contain <func/>, <start/> and <end/>, or <addr/>",
    "bounds: too many nodes: must contain <func/>, <start/> and <end/>, or <addr/>",

    "start/end: node <cu></cu> or attribute 'cu' not found",
    "start/end: node <line></line> or attribute 'line' not found",
    "start/end: invalid compilation unit: cannot be empty",
    "start/end: invalid line number: must be a positive integer",

    "func: invalid compilation unit: cannot be empty",
    "func: attribute 'name' not found",
    "func: invalid name: cannot be empty",

    "addr: no start address",
    "addr: no end address",
    "addr: invalid address value; must be positive, hexadecimal and begin with 0x or 0X",
};

static_assert(static_cast<size_t>(cfg_error_code::ADDR_RANGE_INVALID_VALUE) + 1 ==
    sizeof(error_messages) / sizeof(error_messages[0]),
    "cfg_error_code number of entries does not match message array size");

// begin helper functions

template<typename T>
static std::string remove_spaces(T&& txt)
{
    std::string ret(std::forward<T>(txt));
    ret.erase(std::remove_if(ret.begin(), ret.end(), [](unsigned char c)
        {
            return std::isspace(c);
        }), ret.end());
    return ret;
}

static std::vector<std::string_view> split_line(std::string_view line, std::string_view delim)
{
    std::vector<std::string_view> tokens;
    std::string_view::size_type current = 0;
    std::string_view::size_type next;
    while ((next = line.find_first_of(delim, current)) != std::string_view::npos)
    {
        tokens.emplace_back(&line[current], next - current);
        current = next + delim.length();
    }
    if (current < line.length())
        tokens.emplace_back(&line[current], line.length() - current);
    return tokens;
}

static cfg_expected<config_data::section::target_cont> get_targets(const pugi::xml_node& nsection)
{
    using rettype = cfg_expected<config_data::section::target_cont>;
    using namespace pugi;
    config_data::section::target_cont retval;
    xml_attribute tgt_attr = nsection.attribute("target");
    if (!tgt_attr)
    {
        retval.insert(defaults::target);
        return retval;
    }

    const char_t* tgt_str = tgt_attr.value();
    std::string nospaces = remove_spaces(tgt_str);
    std::vector<std::string_view> tokens = split_line(nospaces, ",");
    for (const auto& target : tokens)
    {
        if (target == "cpu")
            retval.insert(config_data::target::cpu);
        else if (target == "gpu")
            retval.insert(config_data::target::gpu);
        else
            return rettype(nonstd::unexpect, cfg_error_code::SEC_INVALID_TARGET);
    }
    if (retval.empty())
        return rettype(nonstd::unexpect, cfg_error_code::SEC_INVALID_TARGET);
    return retval;
}

static cfg_expected<config_data::params> get_params(const pugi::xml_node& nparams)
{
    using rettype = cfg_expected<config_data::params>;
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
            return rettype(nonstd::unexpect, cfg_error_code::PARAM_INVALID_DOMAIN_MASK);
    xml_node nsockets = nparams.child("socket_mask");
    // <socket_mask></socket_mask> exists
    if (nsockets)
        // <socket_mask></socket_mask> must be a valid, positive integer
        if (!(sktmask = nsockets.text().as_uint(0)))
            return rettype(nonstd::unexpect, cfg_error_code::PARAM_INVALID_SOCKET_MASK);
    xml_node ndevs = nparams.child("device_mask");
    // <device_mask></device_mask> exists
    if (ndevs)
        // <device_mask></device_mask> must be a valid, positive integer
        if (!(devmask = ndevs.text().as_uint(0)))
            return rettype(nonstd::unexpect, cfg_error_code::PARAM_INVALID_DEVICE_MASK);
    return config_data::params{ dommask, sktmask, devmask };
}

static cfg_expected<std::chrono::milliseconds> get_interval(const pugi::xml_node& nsection,
    config_data::profiling_method method)
{
    using rettype = cfg_expected<std::chrono::milliseconds>;
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
            return rettype(nonstd::unexpect, cfg_error_code::SEC_INVALID_INTERVAL);
        eint = std::chrono::milliseconds(interval);
    }
    else if (nfreq)
    {
        // <freq></freq> must be a positive decimal number
        double freq = nfreq.text().as_double(0.0);
        if (freq <= 0.0)
            return rettype(nonstd::unexpect, cfg_error_code::SEC_INVALID_FREQ);
        // clamps at 1000 Hz
        double interval = 1000.0 / freq;
        eint = std::chrono::milliseconds(interval <= 1.0 ? 1 : static_cast<unsigned int>(interval));
    }
    if (method == config_data::profiling_method::energy_total)
        eint = defaults::max_interval;
    return eint;
}

static cfg_expected<uint32_t> get_samples(const pugi::xml_node& nsection,
    const std::chrono::milliseconds& interval)
{
    using rettype = cfg_expected<uint32_t>;
    using namespace pugi;
    xml_node nsamp = nsection.child("samples");
    xml_node ndur = nsection.child("duration");
    if (ndur)
    {
        int duration = ndur.text().as_int(0);
        if (duration <= 0)
            return rettype(nonstd::unexpect, cfg_error_code::SEC_INVALID_DURATION);
        return duration / interval.count() + (duration % interval.count() != 0);
    }
    if (nsamp)
    {
        int samples = nsamp.text().as_int(0);
        if (samples <= 0)
            return rettype(nonstd::unexpect, cfg_error_code::SEC_INVALID_SAMPLES);
        return samples;
    }
    return defaults::samples;
}

static cfg_expected<uint32_t> get_execs(const pugi::xml_node& nsection)
{
    using rettype = cfg_expected<uint32_t>;
    using namespace pugi;
    // <execs></execs> - optional, must be a positive integer
    // if not present - use default value
    xml_node nexecs = nsection.child("execs");
    int execs = 0;
    if (nexecs && (execs = nexecs.text().as_int(0)) <= 0)
        return rettype(nonstd::unexpect, cfg_error_code::SEC_INVALID_EXECS);
    return execs ? static_cast<uint32_t>(execs) : defaults::executions;
}

static cfg_expected<std::string> get_cu(const pugi::xml_node& pos_node)
{
    using rettype = cfg_expected<std::string>;
    using namespace pugi;
    // attribute "cu" exists
    xml_attribute cu_attr = pos_node.attribute("cu");
    if (cu_attr)
    {
        // if attribute exists - it cannot be empty
        if (!*cu_attr.value())
            return rettype(nonstd::unexpect, cfg_error_code::POS_INVALID_COMP_UNIT);
        return cu_attr.value();
    }
    // fallback to checking child node
    xml_node cu = pos_node.child("cu");
    // <cu></cu> exists
    if (!cu)
        return rettype(nonstd::unexpect, cfg_error_code::POS_NO_COMP_UNIT);
    // <cu></cu> is not empty
    if (!*cu.child_value())
        return rettype(nonstd::unexpect, cfg_error_code::POS_INVALID_COMP_UNIT);
    return cu.child_value();
}

static cfg_expected<uint32_t> get_lineno(const pugi::xml_node& pos_node)
{
    using rettype = cfg_expected<uint32_t>;
    using namespace pugi;
    // attribute "line" exists
    xml_attribute line_attr = pos_node.attribute("line");
    if (line_attr)
    {
        // if attribute exists - it cannot be empty
        if (!*line_attr.value())
            return rettype(nonstd::unexpect, cfg_error_code::POS_INVALID_LINE);
        int lineno;
        if ((lineno = line_attr.as_int(0)) <= 0)
            return rettype(nonstd::unexpect, cfg_error_code::POS_INVALID_LINE);
        return lineno;
    }
    // fallback to checking child node
    xml_node line = pos_node.child("line");
    // <line></line> exists
    if (!line)
        return rettype(nonstd::unexpect, cfg_error_code::POS_NO_LINE);
    // <line></line> is not empty or negative
    int lineno;
    if ((lineno = line.text().as_int(0)) <= 0)
        return rettype(nonstd::unexpect, cfg_error_code::POS_INVALID_LINE);
    return lineno;
}

static cfg_expected<config_data::position> get_position(const pugi::xml_node& pos_node)
{
    using rettype = cfg_expected<config_data::position>;
    using namespace pugi;
    cfg_expected<std::string> cu = get_cu(pos_node);
    if (!cu)
        return rettype(nonstd::unexpect, std::move(cu.error()));
    cfg_expected<uint32_t> lineno = get_lineno(pos_node);
    if (!lineno)
        return rettype(nonstd::unexpect, std::move(lineno.error()));
    return config_data::position{ std::move(*cu), *lineno };
}

static cfg_expected<config_data::function> get_function(const pugi::xml_node& func_node)
{
    using rettype = cfg_expected<config_data::function>;
    using namespace pugi;
    // attribute "cu" exists
    std::string cu;
    xml_attribute cu_attr = func_node.attribute("cu");
    if (cu_attr)
    {
        // if attribute exists - it cannot be empty
        if (!*cu_attr.value())
            return rettype(nonstd::unexpect, cfg_error_code::FUNC_INVALID_COMP_UNIT);
        cu.append(cu_attr.value());
    }
    xml_attribute name_attr = func_node.attribute("name");
    if (!name_attr)
        return rettype(nonstd::unexpect, cfg_error_code::FUNC_NO_NAME);
    if (!*name_attr.value())
        return rettype(nonstd::unexpect, cfg_error_code::FUNC_INVALID_NAME);
    return config_data::function(std::move(cu), name_attr.value());
}

static cfg_expected<uint32_t> get_address_value(const pugi::xml_attribute& attr)
{
    using rettype = cfg_expected<uint32_t>;
    using namespace pugi;
    auto valid_prefix = [](std::string_view val)
    {
        return val[0] == '0' && (val[1] == 'x' || val[1] == 'X');
    };
    if (attr.empty())
        return rettype(nonstd::unexpect, cfg_error_code::ADDR_RANGE_INVALID_VALUE);
    std::string_view text = attr.value();
    if (text.size() <= 2 || !valid_prefix(text))
        return rettype(nonstd::unexpect, cfg_error_code::ADDR_RANGE_INVALID_VALUE);
    uint32_t value;
    auto [ptr, ec] = std::from_chars(text.begin() + 2, text.end(), value, 16);
    if (std::make_error_code(ec))
        return rettype(nonstd::unexpect, cfg_error_code::ADDR_RANGE_INVALID_VALUE);
    return value;
}

static cfg_expected<config_data::address_range> get_address_range(const pugi::xml_node& ar_node)
{
    using rettype = cfg_expected<config_data::address_range>;
    using namespace pugi;
    xml_attribute start_attr = ar_node.attribute("start");
    if (!start_attr)
        return rettype(nonstd::unexpect, cfg_error_code::ADDR_RANGE_NO_START);
    xml_attribute end_attr = ar_node.attribute("end");
    if (!end_attr)
        return rettype(nonstd::unexpect, cfg_error_code::ADDR_RANGE_NO_END);
    auto start = get_address_value(start_attr);
    if (!start)
        return rettype(nonstd::unexpect, std::move(start.error()));
    auto end = get_address_value(end_attr);
    if (!end)
        return rettype(nonstd::unexpect, std::move(end.error()));
    return config_data::address_range{ *start, *end };
}

static cfg_expected<config_data::bounds> get_bounds(const pugi::xml_node& bounds)
{
    using rettype = cfg_expected<config_data::bounds>;
    using namespace pugi;
    // <start/>
    xml_node nstart = bounds.child("start");
    // <end/>
    xml_node nend = bounds.child("end");
    // <func/>
    xml_node nfunc = bounds.child("func");
    // <addr/>
    xml_node naddr = bounds.child("addr");

    if ((nstart && nfunc) || (nend && nfunc) ||
        (nstart && naddr) || (nend && naddr) ||
        (nfunc && naddr))
    {
        return rettype(nonstd::unexpect, cfg_error_code::BOUNDS_TOO_MANY);
    }

    if (nstart || nend)
    {
        assert(!nfunc && !naddr);
        if (!nend)
            return rettype(nonstd::unexpect, cfg_error_code::BOUNDS_NO_END);
        if (!nstart)
            return rettype(nonstd::unexpect, cfg_error_code::BOUNDS_NO_START);

        auto pstart = get_position(nstart);
        if (!pstart)
            return rettype(nonstd::unexpect, std::move(pstart.error()));
        auto pend = get_position(nend);
        if (!pend)
            return rettype(nonstd::unexpect, std::move(pend.error()));
        return config_data::bounds{ std::move(*pstart), std::move(*pend) };
    }
    else if (nfunc)
    {
        assert(!nstart && !nend);
        auto func = get_function(nfunc);
        if (!func)
            return rettype(nonstd::unexpect, std::move(func.error()));
        return std::move(*func);
    }
    else if (naddr)
    {
        assert(!nstart && !nend && !nfunc);
        auto range = get_address_range(naddr);
        if (!range)
            return rettype(nonstd::unexpect, std::move(range.error()));
        return std::move(*range);
    }
    return rettype(nonstd::unexpect, cfg_error_code::BOUNDS_EMPTY);
}

static cfg_expected<config_data::profiling_method> get_method(const pugi::xml_node& nsection)
{
    using rettype = cfg_expected<config_data::profiling_method>;
    using namespace pugi;
    config_data::profiling_method method = defaults::method;
    // <method></method> - optional
    xml_node nmethod = nsection.child("method");
    if (nmethod)
    {
        const pugi::char_t* method_str = nmethod.child_value();
        if (!strcmp(method_str, "profile"))
            method = config_data::profiling_method::energy_profile;
        else if (!strcmp(method_str, "total"))
            method = config_data::profiling_method::energy_total;
        else
            return rettype(nonstd::unexpect, cfg_error_code::SEC_INVALID_METHOD);
    }
    return method;
}

static cfg_expected<bool>
get_short(const pugi::xml_node& nsection, config_data::profiling_method method)
{
    using rettype = cfg_expected<bool>;
    using namespace pugi;

    xml_node nshort = nsection.child("short");
    xml_node nlong = nsection.child("long");

    if (nshort && nlong)
        return rettype(nonstd::unexpect, cfg_error_code::SEC_BOTH_SHORT_AND_LONG);
    if (nshort && method == config_data::profiling_method::energy_profile)
        return rettype(nonstd::unexpect, cfg_error_code::SEC_INVALID_METHOD_FOR_SHORT);
    return bool(nshort);
}

static cfg_expected<config_data::section> get_section(const pugi::xml_node& nsection)
{
    using rettype = cfg_expected<config_data::section>;
    using namespace pugi;
    // attribute target
    cfg_expected<config_data::section::target_cont> targets = get_targets(nsection);
    if (!targets)
        return rettype(nonstd::unexpect, std::move(targets.error()));

    // label attribute - optional, must not be empty
    xml_attribute attr_label = nsection.attribute("label");
    if (attr_label && !*attr_label.value())
        return rettype(nonstd::unexpect, cfg_error_code::SEC_INVALID_LABEL);
    // <extra></extra> - optional, must not be empty
    xml_node nxtra = nsection.child("extra");
    if (nxtra && !*nxtra.child_value())
        return rettype(nonstd::unexpect, cfg_error_code::SEC_INVALID_EXTRA);

    cfg_expected<config_data::profiling_method> method = get_method(nsection);
    if (!method)
        return rettype(nonstd::unexpect, std::move(method.error()));

    cfg_expected<bool> is_short = get_short(nsection, *method);
    if (!is_short)
        return rettype(nonstd::unexpect, std::move(is_short.error()));

    cfg_expected<std::chrono::milliseconds> interval = get_interval(nsection, *method);
    if (!interval)
        return rettype(nonstd::unexpect, std::move(interval.error()));

    cfg_expected<uint32_t> execs = get_execs(nsection);
    if (!execs)
        return rettype(nonstd::unexpect, std::move(execs.error()));

    cfg_expected<uint32_t> samples = get_samples(nsection, *interval);
    if (!samples)
        return rettype(nonstd::unexpect, std::move(samples.error()));

    // <bounds></bounds>
    xml_node nbounds = nsection.child("bounds");
    if (!nbounds)
        return rettype(nonstd::unexpect, cfg_error_code::SEC_NO_BOUNDS);
    cfg_expected<config_data::bounds> bounds = get_bounds(nbounds);
    if (!bounds)
        return rettype(nonstd::unexpect, std::move(bounds.error()));

    // <allow_concurrency/> - true if node exists, false otherwise
    bool allow_concurrency = bool(nsection.child("allow_concurrency"));

    return config_data::section
    {
        attr_label.value(),
        nxtra.child_value(),
        std::move(*targets),
        *method,
        std::move(*bounds),
        std::move(*interval),
        *execs,
        *samples,
        allow_concurrency,
        *is_short
    };
}

static cfg_expected<config_data::section_group> get_group(const pugi::xml_node& nsections)
{
    using rettype = cfg_expected<config_data::section_group>;
    using namespace pugi;
    // label attribute - optional, must not be empty
    xml_attribute attr_label = nsections.attribute("label");
    if (attr_label && !*attr_label.value())
        return rettype(nonstd::unexpect, cfg_error_code::GROUP_INVALID_LABEL);

    // <extra></extra> - optional, must not be empty
    xml_node nxtra = nsections.child("extra");
    if (nxtra && !*nxtra.child_value())
        return rettype(nonstd::unexpect, cfg_error_code::GROUP_INVALID_EXTRA);

    config_data::section_group group{ attr_label.value(), nxtra.child_value() };
    // <section></section> - at least one required
    for (xml_node nsection = nsections.child("section");
        nsection;
        nsection = nsection.next_sibling("section"))
    {
        auto section = get_section(nsection);
        if (!section)
            return rettype(nonstd::unexpect, std::move(section.error()));
        if (!group.push_back(std::move(*section)))
            return rettype(nonstd::unexpect, cfg_error_code::SEC_LABEL_ALREADY_EXISTS);
    }
    if (group.sections().empty())
        return rettype(nonstd::unexpect, cfg_error_code::GROUP_EMPTY);
    return group;
}

// end helper functions

// address_range

config_data::address_range::address_range(uint32_t start, uint32_t end) :
    _start(start),
    _end(end)
{}

uint32_t config_data::address_range::start() const noexcept
{
    return _start;
}

uint32_t config_data::address_range::end() const noexcept
{
    return _end;
}

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
    function,
    address_range,
};

void config_data::bounds::copy_data(const bounds& other)
{
    switch (_tag)
    {
    case type::position:
        new (&_positions.start) auto(other._positions.start);
        new (&_positions.end) auto(other._positions.end);
        return;
    case type::function:
        new (&_func) auto(other._func);
        return;
    case type::address_range:
        new (&_addr) auto(other._addr);
        return;
    }
    assert(false);
}

void config_data::bounds::move_data(bounds&& other)
{
    switch (_tag)
    {
    case type::position:
        new (&_positions.start) auto(std::move(other._positions.start));
        new (&_positions.end) auto(std::move(other._positions.end));
        return;
    case type::function:
        new (&_func) auto(std::move(other._func));
        return;
    case type::address_range:
        new (&_addr) auto(std::move(other._addr));
        return;
    }
    assert(false);
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

config_data::bounds::bounds(address_range ar) :
    _tag(type::address_range),
    _addr(ar)
{}

config_data::bounds::~bounds()
{
    switch (_tag)
    {
    case type::position:
        _positions.start.~position();
        _positions.end.~position();
        return;
    case type::function:
        _func.~function();
        return;
    case type::address_range:
        _addr.~address_range();
        return;
    }
    assert(false);
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
    if (this != &other)
    {
        if (_tag != other._tag)
        {
            this->~bounds();
            _tag = other._tag;
        }
        copy_data(other);
    }
    return *this;
}

config_data::bounds& config_data::bounds::operator=(bounds&& other)
{
    if (this != &other)
    {
        if (_tag != other._tag)
        {
            this->~bounds();
            _tag = std::move(other._tag);
        }
        move_data(std::move(other));
    }
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

bool config_data::bounds::has_address_range() const
{
    return _tag == type::address_range;
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

config_data::address_range config_data::bounds::addr_range() const
{
    assert(_tag == type::address_range);
    return _addr;
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

const std::string& config_data::section::label() const
{
    return _label;
}

const std::string& config_data::section::extra() const
{
    return _extra;
}

const config_data::section::target_cont& config_data::section::targets() const
{
    return _targets;
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

bool config_data::section::has_label() const
{
    return !_label.empty();
}

bool config_data::section::has_extra() const
{
    return !_extra.empty();
}

bool config_data::section::allow_concurrency() const
{
    return _concurrency;
}

bool config_data::section::is_short() const
{
    return _isshort;
}


// section_group

config_data::section_group::section_group(const std::string& label, const std::string& extra) :
    _label(label),
    _extra(extra)
{}

config_data::section_group::section_group(const std::string& label, std::string&& extra) :
    _label(label),
    _extra(std::move(extra))
{}

config_data::section_group::section_group(std::string&& label, const std::string& extra) :
    _label(std::move(label)),
    _extra(extra)
{}

config_data::section_group::section_group(std::string&& label, std::string&& extra) :
    _label(std::move(label)),
    _extra(std::move(extra))
{}

const std::string& config_data::section_group::label() const
{
    return _label;
}

const std::string& config_data::section_group::extra() const
{
    return _extra;
}

const std::vector<config_data::section>& config_data::section_group::sections() const
{
    return _sections;
}

bool config_data::section_group::has_label() const
{
    return !_label.empty();
}

bool config_data::section_group::has_extra() const
{
    return !_extra.empty();
}

bool config_data::section_group::has_section_with(config_data::target tgt) const
{
    for (const auto& sec : _sections)
        for (auto t : sec.targets())
            if (t == tgt)
                return true;
    return false;
}

bool config_data::section_group::has_section_with(config_data::profiling_method method) const
{
    for (const auto& sec : _sections)
        if (sec.method() == method)
            return true;
    return false;
}

bool config_data::section_group::push_back(const section& sec)
{
    return push_back_impl(sec);
}

bool config_data::section_group::push_back(section&& sec)
{
    return push_back_impl(sec);
}

template<typename Sec>
bool config_data::section_group::push_back_impl(Sec&& sec)
{
    // check if section with same label exists
    for (const auto& section : _sections)
        if (section.label() == sec.label())
            return false;
    _sections.push_back(std::forward<Sec>(sec));
    return true;
}


// config_data

config_data::config_data(const config_data::params& params) :
    _parameters(params)
{}

const config_data::params& config_data::parameters() const
{
    return _parameters;
}

const std::vector<config_data::section_group>& config_data::groups() const
{
    return _groups;
}

std::vector<const config_data::section*> config_data::flat_sections() const&
{
    std::vector<const config_data::section*> retval;
    for (const auto& g : _groups)
        for (const auto& s : g.sections())
            retval.push_back(&s);
    return retval;
}

bool config_data::has_section_with(config_data::target tgt) const
{
    for (const auto& g : _groups)
        if (g.has_section_with(tgt))
            return true;
    return false;
}

bool config_data::has_section_with(config_data::profiling_method method) const
{
    for (const auto& g : _groups)
        if (g.has_section_with(method))
            return true;
    return false;
}

bool config_data::push_back(const section_group& grp)
{
    return push_back_impl(grp);
}

bool config_data::push_back(section_group&& grp)
{
    return push_back_impl(grp);
}

template<typename Group>
bool config_data::push_back_impl(Group&& grp)
{
    // check if group with same label already exists
    for (const auto& g : _groups)
        if (g.label() == grp.label())
            return false;
    _groups.push_back(std::forward<Group>(grp));
    return true;
}


// load_config

cfg_expected<config_data> tep::load_config(std::istream& from)
{
    using namespace pugi;
    using rettype = cfg_expected<config_data>;
    xml_document doc;
    xml_parse_result parse_result = doc.load(from);
    if (!parse_result)
    {
        switch (parse_result.status)
        {
        case status_file_not_found:
            return rettype(nonstd::unexpect, cfg_error_code::CONFIG_NOT_FOUND);
        case status_io_error:
            return rettype(nonstd::unexpect, cfg_error_code::CONFIG_IO_ERROR);
        case status_out_of_memory:
            return rettype(nonstd::unexpect, cfg_error_code::CONFIG_OUT_OF_MEM);
        default:
            return rettype(nonstd::unexpect, cfg_error_code::CONFIG_BAD_FORMAT);
        }
    }
    // <config></config>
    xml_node nconfig = doc.child("config");
    if (!nconfig)
        return rettype(nonstd::unexpect, cfg_error_code::CONFIG_NO_CONFIG);

    // <params></params> - optional, use default values if does not exist
    config_data::params params;
    xml_node nparams = nconfig.child("params");
    if (nparams)
    {
        auto custom_params = get_params(nparams);
        if (!custom_params)
            return rettype(nonstd::unexpect, std::move(custom_params.error()));
        params = std::move(*custom_params);
    }
    config_data cfgdata(params);

    // iterate all section groups
    // <sections></sections> - optional
    for (xml_node nsections = nconfig.child("sections");
        nsections;
        nsections = nsections.next_sibling("sections"))
    {
        auto group = get_group(nsections);
        if (!group)
            return rettype(nonstd::unexpect, std::move(group.error()));
        if (!cfgdata.push_back(std::move(*group)))
            return rettype(nonstd::unexpect, cfg_error_code::GROUP_LABEL_ALREADY_EXISTS);
    }
    return cfgdata;
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

std::ostream& tep::operator<<(std::ostream& os, const config_data::address_range& a)
{
    std::ios::fmtflags flags(os.flags());
    os << std::hex << "0x" << a.start() << "-0x" << a.end();
    os.flags(flags);
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
    case config_data::bounds::type::address_range:
        os << b.addr_range();
        break;
    }
    return os;
}

std::ostream& tep::operator<<(std::ostream& os, const config_data::section& s)
{
    os << "label: " << (s.has_label() ? s.label() : "-");
    os << "\nextra: " << (s.has_extra() ? s.extra() : "-");
    os << "\ntarget: " << s.targets();
    os << "\ninterval: " << s.interval().count() << " ms";
    os << "\nmethod: " << s.method();
    os << "\nbounds: " << s.bounds();
    os << "\nexecutions: " << s.executions();
    os << "\nsamples: " << s.samples();
    os << "\nallow concurrency: " << (s.allow_concurrency() ? "true" : "false");
    os << "\nshort: " << (s.is_short() ? "true" : "false");
    return os;
}

std::ostream& tep::operator<<(std::ostream& os, const config_data& cd)
{
    os << cd.parameters() << "\n";
    os << "groups (" << cd.groups().size() << "):";
    for (const auto& g : cd.groups())
        os << "\n----------\n" << g;
    return os;
}

std::ostream& tep::operator<<(std::ostream& os, const config_data::section::target_cont& tgts)
{
    for (auto tgt : tgts)
        os << tgt << " ";
    return os;
}

std::ostream& tep::operator<<(std::ostream& os, const config_data::section_group& g)
{
    os << "label: " << (g.has_label() ? g.label() : "-") << "\n";
    os << "extra: " << (g.has_extra() ? g.extra() : "-") << "\n";
    os << "sections (" << g.sections().size() << "):";
    for (const auto& section : g.sections())
        os << "\n----------\n" << section;
    return os;
}


bool tep::operator==(const config_data::params& lhs, const config_data::params& rhs)
{
    return lhs.domain_mask() == rhs.domain_mask() &&
        lhs.socket_mask() == rhs.socket_mask() &&
        lhs.device_mask() == rhs.device_mask();
}

bool tep::operator==(const config_data::address_range& lhs, const config_data::address_range& rhs)
{
    return lhs.start() == rhs.start() && lhs.end() == rhs.end();
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
    return lhs.label() == rhs.label() && lhs.extra() == rhs.extra() &&
        lhs.targets() == rhs.targets() && lhs.method() == rhs.method() &&
        lhs.bounds() == rhs.bounds() && lhs.interval() == rhs.interval() &&
        lhs.executions() == rhs.executions() && lhs.samples() == lhs.samples();
}

bool operator==(const config_data::section_group& lhs, const config_data::section_group& rhs)
{
    return lhs.label() == rhs.label() && lhs.sections() == rhs.sections();
}
