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

static constexpr std::string_view error_messages[] =
{
    "No error",

    "I/O error when loading config file",
    "Config file not found",
    "Out of memory when loading config file",
    "Config file is badly formatted",
    "Node <config></config> not found",

    "section: Node <bounds></bounds> not found",
    "section: Node <freq></freq> not found",
    "section: Node <interval></interval> not found",
    "section: Node <method></method> not found",
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

static_assert(static_cast<size_t>(tep::cfg::error::code_t::addr_range_invalid_value) + 1 ==
    sizeof(error_messages) / sizeof(error_messages[0]),
    "cfg_error_code number of entries does not match message array size");

namespace
{
    template<typename T>
    std::string remove_spaces(T&& txt)
    {
        std::string ret(std::forward<T>(txt));
        ret.erase(std::remove_if(ret.begin(), ret.end(), [](unsigned char c)
            {
                return std::isspace(c);
            }), ret.end());
        return ret;
    }

    std::vector<std::string_view> split_line(std::string_view line, std::string_view delim)
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

    std::string to_lower_case(std::string_view x)
    {
        std::string retval(x);
        std::transform(x.begin(), x.end(), retval.begin(),
            [](unsigned char c)
            {
                return std::tolower(c);
            });
        return retval;
    }

    template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
    template<class... Ts> overloaded(Ts...)->overloaded<Ts...>;

    template<typename>
    struct indentation;
    template<>
    struct indentation<tep::cfg::section_t> : std::integral_constant<size_t, 4> {};
    template<>
    struct indentation<tep::cfg::group_t> : std::integral_constant<size_t, 2> {};

    template<typename T>
    std::ostream& operator<<(std::ostream& os, indentation<T>)
    {
        os << std::right << std::setw(indentation<T>::value) << "";
        return os;
    }

    tep::cfg::result<uint32_t> get_address_value(const pugi::xml_attribute& attr)
    {
        using rettype = decltype(get_address_value(std::declval<decltype(attr)>()));
        using tep::cfg::error;
        using namespace pugi;

        auto valid_prefix = [](std::string_view val)
        {
            return val[0] == '0' && (val[1] == 'x' || val[1] == 'X');
        };

        if (attr.empty())
            return rettype(nonstd::unexpect, error::code_t::addr_range_invalid_value);
        std::string_view text = attr.value();
        if (text.size() <= 2 || !valid_prefix(text))
            return rettype(nonstd::unexpect, error::code_t::addr_range_invalid_value);
        uint32_t value;
        auto [ptr, ec] = std::from_chars(text.begin() + 2, text.end(), value, 16);
        if (std::make_error_code(ec))
            return rettype(nonstd::unexpect, error::code_t::addr_range_invalid_value);
        return value;
    }

    tep::cfg::result<std::string> get_cu(const pugi::xml_node& pos_node)
    {
        using rettype = decltype(get_cu(std::declval<decltype(pos_node)>()));
        using tep::cfg::error;
        using namespace pugi;
        // attribute "cu" exists
        xml_attribute cu_attr = pos_node.attribute("cu");
        if (cu_attr)
        {
            // if attribute exists - it cannot be empty
            if (!*cu_attr.value())
                return rettype(nonstd::unexpect, error::code_t::pos_invalid_comp_unit);
            return cu_attr.value();
        }
        // fallback to checking child node
        xml_node cu = pos_node.child("cu");
        // <cu></cu> exists
        if (!cu)
            return rettype(nonstd::unexpect, error::code_t::pos_no_comp_unit);
        // <cu></cu> is not empty
        if (!*cu.child_value())
            return rettype(nonstd::unexpect, error::code_t::pos_invalid_comp_unit);
        return cu.child_value();
    }

    tep::cfg::result<uint32_t> get_lineno(const pugi::xml_node& pos_node)
    {
        using rettype = decltype(get_lineno(std::declval<decltype(pos_node)>()));
        using tep::cfg::error;
        using namespace pugi;
        // attribute "line" exists
        xml_attribute line_attr = pos_node.attribute("line");
        if (line_attr)
        {
            // if attribute exists - it cannot be empty
            if (!*line_attr.value())
                return rettype(nonstd::unexpect, error::code_t::pos_invalid_line);
            int lineno;
            if ((lineno = line_attr.as_int(0)) <= 0)
                return rettype(nonstd::unexpect, error::code_t::pos_invalid_line);
            return lineno;
        }
        // fallback to checking child node
        xml_node line = pos_node.child("line");
        // <line></line> exists
        if (!line)
            return rettype(nonstd::unexpect, error::code_t::pos_no_line);
        // <line></line> is not empty or negative
        int lineno;
        if ((lineno = line.text().as_int(0)) <= 0)
            return rettype(nonstd::unexpect, error::code_t::pos_invalid_line);
        return lineno;
    }

    bool valid_hex_prefix(std::string_view x) noexcept
    {
        return x.size() > 2 && x[0] == '0' && (x[1] == 'x' || x[1] == 'X');
    }

    std::optional<uint32_t> get_hex_value(std::string_view data) noexcept
    {
        if (valid_hex_prefix(data))
        {
            uint32_t value;
            auto [ptr, ec] = std::from_chars(data.begin() + 2, data.end(), value, 16);
            if (!std::make_error_code(ec))
                return value;
        }
        return std::nullopt;
    }

    tep::cfg::result<std::chrono::milliseconds> get_interval(const pugi::xml_node& nsection)
    {
        using namespace pugi;
        using tep::cfg::error;
        using rettype = decltype(get_interval(std::declval<decltype(nsection)>()));
        xml_node nfreq = nsection.child("freq");
        xml_node nint = nsection.child("interval");
        // <interval/> overrides <freq></freq>
        if (nint)
        {
            // <interval/> must be a valid, positive integer
            int interval = nint.text().as_int(0);
            if (interval <= 0)
                return rettype(nonstd::unexpect, error::code_t::sec_invalid_interval);
            return std::chrono::milliseconds(interval);
        }
        if (nfreq)
        {
            // <freq/> must be a positive decimal number
            double freq = nfreq.text().as_double(0.0);
            if (freq <= 0.0)
                return rettype(nonstd::unexpect, error::code_t::sec_invalid_freq);
            return std::chrono::milliseconds(
                static_cast<std::chrono::milliseconds::rep>(
                    std::clamp(1000.0 / freq, 1.0, 1000.0 / freq)));
        }
        return rettype(nonstd::unexpect, error::code_t::sec_no_interval);
    }

    tep::cfg::result<std::optional<uint32_t>> get_samples(
        const pugi::xml_node& nsection,
        const std::chrono::milliseconds& interval)
    {
        using namespace pugi;
        using tep::cfg::error;
        using rettype = tep::cfg::result<std::optional<uint32_t>>;
        xml_node nsamp = nsection.child("samples");
        xml_node ndur = nsection.child("duration");
        // <duration/> overwrites <samples/>
        if (ndur)
        {
            // <duration/> must be a valid, positive integer
            int duration = ndur.text().as_int(0);
            if (duration <= 0)
                return rettype(nonstd::unexpect, error::code_t::sec_invalid_duration);
            return duration / interval.count() + (duration % interval.count() != 0);
        }
        if (nsamp)
        {
            // <samples/> must be a valid, positive integer
            int samples = nsamp.text().as_int(0);
            if (samples <= 0)
                return rettype(nonstd::unexpect, error::code_t::sec_invalid_samples);
            return samples;
        }
        return std::nullopt;
    }

    tep::cfg::result<std::string> get_method(const pugi::xml_node& nsection)
    {
        using namespace pugi;
        using tep::cfg::error;
        using rettype = decltype(get_method(std::declval<decltype(nsection)>()));
        xml_node nmethod = nsection.child("method");
        if (!nmethod)
            return rettype(nonstd::unexpect, error::code_t::sec_no_method);
        std::string method = to_lower_case(nmethod.child_value());
        if (method == "profile" || method == "total")
            return method;
        return rettype(nonstd::unexpect, error::code_t::sec_invalid_method);
    }

    tep::cfg::result<tep::cfg::target> get_targets(const pugi::xml_attribute& attr)
    {
        using namespace pugi;
        using tep::cfg::error;
        using rettype = decltype(get_targets(std::declval<decltype(attr)>()));

        tep::cfg::target retval = static_cast<tep::cfg::target>(0);
        std::string targets = to_lower_case(remove_spaces(attr.value()));
        std::vector<std::string_view> tokens = split_line(targets, ",");
        for (auto target : tokens)
        {
            if (target == "cpu")
                retval |= tep::cfg::target::cpu;
            else if (target == "gpu")
                retval |= tep::cfg::target::gpu;
            else
                return rettype(nonstd::unexpect, error::code_t::sec_invalid_target);
        }
        if (!target_valid(retval))
            return rettype(nonstd::unexpect, error::code_t::sec_invalid_target);
        return retval;
    }
}

namespace tep::cfg
{
    struct config_entry
    {
        pugi::xml_node node;

        explicit operator bool() const noexcept
        {
            return bool(node);
        }
    };

    error error::success() noexcept
    {
        return error{ code_t::success };
    }

    error::error(code_t code) noexcept :
        _code(code)
    {}

    error::operator bool() const noexcept
    {
        return _code != code_t::success;
    }

    error::code_t error::code() const noexcept
    {
        return _code;
    }

    bool target_valid(target x) noexcept
    {
        return bool(static_cast<std::underlying_type_t<target>>(x));
    }

    target target_next(target x) noexcept
    {
        return static_cast<target>(std::underlying_type_t<target>(x) << 1);
    }

    target operator|(target x, target y) noexcept
    {
        using underlying_t = std::underlying_type_t<target>;
        return static_cast<target>(
            static_cast<underlying_t>(x) | static_cast<underlying_t>(y));
    }

    target operator&(target x, target y) noexcept
    {
        using underlying_t = std::underlying_type_t<target>;
        return static_cast<target>(
            static_cast<underlying_t>(x) & static_cast<underlying_t>(y));
    }

    target operator^(target x, target y) noexcept
    {
        using underlying_t = std::underlying_type_t<target>;
        return static_cast<target>(
            static_cast<underlying_t>(x) ^ static_cast<underlying_t>(y));
    }

    target operator~(target x) noexcept
    {
        return static_cast<target>(
            ~static_cast<std::underlying_type_t<target>>(x));
    }

    target& operator|=(target& x, target y) noexcept
    {
        return x = x | y;
    }

    target& operator&=(target& x, target y) noexcept
    {
        return x = x & y;
    }

    target& operator^=(target& x, target y) noexcept
    {
        return x = x ^ y;
    }

    address_range_t::address_range_t(const config_entry& entry, error& e) noexcept
    {
        using namespace pugi;
        if (e)
            return;
        xml_attribute start_attr = entry.node.attribute("start");
        if (!start_attr && (e = error::code_t::addr_range_no_start))
            return;
        xml_attribute end_attr = entry.node.attribute("end");
        if (!end_attr && (e = error::code_t::addr_range_no_end))
            return;
        auto res_start = get_address_value(start_attr);
        if (!res_start && (e = std::move(res_start.error())))
            return;
        auto res_end = get_address_value(end_attr);
        if (!res_end && (e = std::move(res_end.error())))
            return;
        start = *res_start;
        end = *res_end;
    }

    result<address_range_t> address_range_t::create(const config_entry& entry) noexcept
    {
        error e = error::success();
        address_range_t ar(entry, e);
        if (e)
            return result<address_range_t>(nonstd::unexpect, std::move(e));
        return ar;
    }

    position_t::position_t(const config_entry& entry, error& e)
    {
        using namespace pugi;
        if (e)
            return;
        auto cu = get_cu(entry.node);
        if (!cu && (e = std::move(cu.error())))
            return;
        auto lineno = get_lineno(entry.node);
        if (!lineno && (e = std::move(lineno.error())))
            return;
        compilation_unit = std::move(*cu);
        line = *lineno;
    }

    result<position_t> position_t::create(const config_entry& entry)
    {
        error e = error::success();
        position_t p(entry, e);
        if (e)
            return result<position_t>(nonstd::unexpect, std::move(e));
        return p;
    }

    function_t::function_t(const config_entry& entry, error& e) :
        compilation_unit(std::nullopt)
    {
        using namespace pugi;
        if (e)
            return;
        // attribute "cu" exists
        xml_attribute cu_attr = entry.node.attribute("cu");
        if (cu_attr)
        {
            // if attribute exists - it cannot be empty
            if (!*cu_attr.value() && (e = error::code_t::func_invalid_comp_unit))
                return;
            compilation_unit = cu_attr.value();
        }
        xml_attribute name_attr = entry.node.attribute("name");
        if (!name_attr && (e = error::code_t::func_no_name))
            return;
        if (!*name_attr.value() && (e = error::code_t::func_invalid_name))
            return;
        name = name_attr.value();
    }

    result<function_t> function_t::create(const config_entry& entry)
    {
        error e = error::success();
        function_t f(entry, e);
        if (e)
            return result<function_t>(nonstd::unexpect, std::move(e));
        return f;
    }

    bounds_t::bounds_t(const config_entry& entry, error& e, key<section_t>)
    {
        using namespace pugi;
        if (e)
            return;
        // <start/>
        config_entry nstart{ entry.node.child("start") };
        // <end/>
        config_entry nend{ entry.node.child("end") };
        // <func/>
        config_entry nfunc{ entry.node.child("func") };
        // <addr/>
        config_entry naddr{ entry.node.child("addr") };

        if ((nstart && nfunc) || (nend && nfunc) ||
            (nstart && naddr) || (nend && naddr) ||
            (nfunc && naddr))
        {
            e = error::code_t::bounds_too_many;
        }
        else if (nstart || nend)
        {
            assert(!nfunc && !naddr);
            if (!nend && (e = error::code_t::bounds_no_end))
                return;
            if (!nstart && (e = error::code_t::bounds_no_start))
                return;
            auto pstart = position_t::create(nstart);
            if (!pstart && (e = std::move(pstart.error())))
                return;
            auto pend = position_t::create(nend);
            if (!pend && (e = std::move(pend.error())))
                return;
            _value = position_range_t{ std::move(*pstart), std::move(*pend) };
        }
        else if (nfunc)
        {
            assert(!nstart && !nend && !naddr);
            auto func = function_t::create(nfunc);
            if (!func && (e = std::move(func.error())))
                return;
            _value = std::move(*func);
        }
        else if (naddr)
        {
            assert(!nstart && !nend && !nfunc);
            auto range = address_range_t::create(naddr);
            if (!range && (e = std::move(range.error())))
                return;
            _value = std::move(*range);
        }
        else
        {
            e = error::code_t::bounds_empty;
        }
    }

    result<bounds_t> bounds_t::create(const config_entry& entry, key<section_t> k)
    {
        error e = error::success();
        bounds_t b(entry, e, k);
        if (e)
            return result<bounds_t>(nonstd::unexpect, std::move(e));
        return b;
    }

    params_t::params_t(const config_entry& entry, error& e) noexcept
    {
        using namespace pugi;
        if (e)
            return;
        // <domain_mask/>
        if (xml_node ndomains = entry.node.child("domain_mask"))
        {
            if (auto val = get_hex_value(ndomains.child_value()))
                domain_mask = val;
            else
            {
                e = error::code_t::param_invalid_domain_mask;
                return;
            }
        }
        // <socket_mask/>
        if (xml_node nsockets = entry.node.child("socket_mask"))
        {
            if (auto val = get_hex_value(nsockets.child_value()))
                socket_mask = val;
            else
            {
                e = error::code_t::param_invalid_socket_mask;
                return;
            }
        }
        // <device_mask/>
        if (xml_node ndevs = entry.node.child("device_mask"))
        {
            if (auto val = get_hex_value(ndevs.child_value()))
                device_mask = val;
            else
            {
                e = error::code_t::param_invalid_device_mask;
                return;
            }
        }
    }

    result<params_t> params_t::create(const config_entry& entry) noexcept
    {
        error e = error::success();
        params_t p(entry, e);
        if (e)
            return result<params_t>(nonstd::unexpect, std::move(e));
        return p;
    }

    method_total_t::method_total_t(const config_entry& entry, error& e) noexcept
    {
        using namespace pugi;
        if (e)
            return;
        xml_node nshort = entry.node.child("short");
        xml_node nlong = entry.node.child("long");
        if (nshort && nlong && (e = error::code_t::sec_both_short_and_long))
            return;
        auto res_method = get_method(entry.node);
        if (!res_method && (e = std::move(res_method.error())))
            return;
        if (*res_method == "total" && (e = error::code_t::sec_invalid_method_for_short))
            return;
        short_section = bool(nshort);
    }

    result<method_total_t> method_total_t::create(const config_entry& entry) noexcept
    {
        error e = error::success();
        method_total_t m(entry, e);
        if (e)
            return result<method_total_t>(nonstd::unexpect, std::move(e));
        return m;
    }

    method_profile_t::method_profile_t(const config_entry& entry, error& e) noexcept
    {
        using namespace pugi;
        if (e)
            return;
        auto res_interval = get_interval(entry.node);
        if (!res_interval && (e = std::move(res_interval.error())))
            return;
        auto res_samples = get_samples(entry.node, *res_interval);
        if (!res_samples && (e = std::move(res_samples.error())))
            return;
        interval = *std::move(res_interval);
        samples = *std::move(res_samples);
    }

    result<method_profile_t> method_profile_t::create(const config_entry& entry) noexcept
    {
        error e = error::success();
        method_profile_t m(entry, e);
        if (e)
            return result<method_profile_t>(nonstd::unexpect, std::move(e));
        return m;
    }

    misc_attributes_t::misc_attributes_t(const config_entry& entry, error& e, key<section_t>)
    {
        if (e)
            return;
        auto res_method = get_method(entry.node);
        if (!res_method && (e = std::move(res_method.error())))
            return;
        if (*res_method == "total")
        {
            auto val = method_total_t::create(entry);
            if (!val && (e = std::move(val.error())))
                return;
            _value = *std::move(val);
        }
        else if (*res_method == "profile")
        {
            auto val = method_profile_t::create(entry);
            if (!val && (e = std::move(val.error())))
                return;
            _value = *std::move(val);
        }
        else
        {
            assert(false);
            e = error::code_t::sec_invalid_method;
        }
    }

    result<misc_attributes_t> misc_attributes_t::create(const config_entry& entry, key<section_t> k)
    {
        error e = error::success();
        misc_attributes_t m(entry, e, k);
        if (e)
            return result<misc_attributes_t>(nonstd::unexpect, std::move(e));
        return m;
    }

    section_t::section_t(const config_entry& entry, error& e) :
        label(std::nullopt),
        extra(std::nullopt),
        targets(target::cpu),
        misc(entry, e, key<section_t>{}),
        bounds(config_entry{ entry.node.child("bounds") }, e, key<section_t>{}),
        allow_concurrency(bool(entry.node.child("allow_concurrency")))
    {
        using namespace pugi;
        if (e)
            return;
        if (xml_attribute label_attr = entry.node.attribute("label"))
        {
            if (!*label_attr.value() && (e = error::code_t::sec_invalid_label))
                return;
            label = label_attr.value();
        }
        if (xml_node nextra = entry.node.child("extra"))
        {
            if (!*nextra.child_value() && (e = error::code_t::sec_invalid_extra))
                return;
            extra = nextra.child_value();
        }
        if (xml_attribute target_attr = entry.node.attribute("target"))
        {
            auto res_targets = get_targets(target_attr);
            if (!res_targets && (e = std::move(res_targets).error()))
                return;
            targets = *std::move(res_targets);
        }
    }

    result<section_t> section_t::create(const config_entry& entry)
    {
        error e = error::success();
        section_t s(entry, e);
        if (e)
            return result<section_t>(nonstd::unexpect, std::move(e));
        return s;
    }

    group_t::group_t(const config_entry& entry, error& e) :
        label(std::nullopt),
        extra(std::nullopt)
    {
        using namespace pugi;
        if (xml_attribute label_attr = entry.node.attribute("label"))
        {
            if (!*label_attr.value() && (e = error::code_t::group_invalid_label))
                return;
            label = label_attr.value();
        }
        if (xml_node nextra = entry.node.child("extra"))
        {
            if (!*nextra.child_value() && (e = error::code_t::group_invalid_extra))
                return;
            extra = nextra.child_value();
        }
        // <section></section> - at least one required
        for (config_entry nsection{ entry.node.child("section") };
            nsection;
            nsection = config_entry{ nsection.node.next_sibling("section") })
        {
            auto sec = section_t::create(nsection);
            if (!sec && (e = std::move(sec.error())))
                return;
            auto it = std::find_if(sections.begin(), sections.end(), [&sec](const section_t& s)
                {
                    // label is necessary if more than one group exists
                    // two labels cannot be the same
                    return !s.label.has_value() || !sec->label.has_value()
                        || *s.label == *sec->label;
                });
            if ((it != sections.end()) && (e = error::code_t::sec_label_already_exists))
                return;
            sections.push_back(std::move(*sec));
        }
        if (sections.empty())
            e = error::code_t::group_empty;
    }

    result<group_t> group_t::create(const config_entry& entry)
    {
        error e = error::success();
        group_t g(entry, e);
        if (e)
            return result<group_t>(nonstd::unexpect, std::move(e));
        return g;
    }

    struct config_t::impl
    {
        std::optional<params_t> parameters;
        std::vector<group_t> groups;

        impl(std::istream&, error&);
    };

    config_t::impl::impl(std::istream& is, error& e) :
        parameters(std::nullopt)
    {
        using namespace pugi;
        xml_document doc;
        xml_parse_result parse_result = doc.load(is);
        if (!parse_result)
        {
            switch (parse_result.status)
            {
            case status_file_not_found:
                e = error::code_t::config_not_found;
                break;
            case status_io_error:
                e = error::code_t::config_io_error;
                break;
            case status_out_of_memory:
                e = error::code_t::config_out_of_mem;
                break;
            default:
                e = error::code_t::config_bad_format;
                break;
            }
            return;
        }
        // <config></config>
        xml_node nconfig = doc.child("config");
        if (!nconfig && (e = error::code_t::config_no_config))
            return;
        // <params></params> - optional
        auto params = params_t::create(config_entry{ nconfig.child("params") });
        if (!params && (e = std::move(params.error())))
            return;
        for (config_entry nsections{ nconfig.child("sections") };
            nsections;
            nsections = config_entry{ nsections.node.next_sibling("sections") })
        {
            auto grp = group_t::create(nsections);
            if (!grp && (e = std::move(grp.error())))
                return;
            auto it = std::find_if(groups.begin(), groups.end(), [&grp](const group_t& g)
                {
                    // label is necessary if more than one group exists
                    // two labels cannot be the same
                    return !g.label.has_value() || !grp->label.has_value()
                        || *g.label == *grp->label;
                });
            if ((it != groups.end()) && (e = error::code_t::group_label_already_exists))
                return;
            groups.push_back(std::move(*grp));
        }
        parameters = std::move(*params);
    }

    config_t::config_t(std::istream& is, error& e) :
        _impl(std::make_shared<impl>(is, e))
    {}

    result<config_t> config_t::create(std::istream& is)
    {
        error e = error::success();
        config_t c(is, e);
        if (e)
            return result<config_t>(nonstd::unexpect, std::move(e));
        return c;
    }

    const std::optional<params_t>& config_t::parameters() const noexcept
    {
        return _impl->parameters;
    }

    const std::vector<group_t>& config_t::groups() const noexcept
    {
        return _impl->groups;
    }

    std::ostream& operator<<(std::ostream& os, const error& e)
    {
        os << error_messages[static_cast<size_t>(e.code())];
        return os;
    }

    std::ostream& operator<<(std::ostream& os, const target& t)
    {
        for (target tmp = t, curr = target::cpu;
            target_valid(tmp);
            tmp &= ~curr, curr = target_next(curr))
        {
            switch (tmp & curr)
            {
            case target::cpu:
                os << "cpu";
                if (target_valid(tmp & ~curr))
                    os << ",";
                break;
            case target::gpu:
                os << "gpu";
                if (target_valid(tmp & ~curr))
                    os << ",";
                break;
            }
        }
        return os;
    }

    std::ostream& operator<<(std::ostream& os, const params_t& p)
    {
        std::ios::fmtflags flags(os.flags());
        os << "domains: ";
        if (p.domain_mask)
            os << "0x" << std::hex << *p.domain_mask;
        else
            os << "n/a";
        os << ", ";
        os << "sockets: ";
        if (p.socket_mask)
            os << "0x" << std::hex << *p.socket_mask;
        else
            os << "n/a";
        os << ", ";
        os << "devices: ";
        if (p.device_mask)
            os << "0x" << std::hex << *p.device_mask;
        else
            os << "n/a";
        os.flags(flags);
        return os;
    }

    std::ostream& operator<<(std::ostream& os, const address_range_t& x)
    {
        std::ios::fmtflags flags(os.flags());
        os << std::hex << "0x" << x.start << "-0x" << x.end;
        os.flags(flags);
        return os;
    }

    std::ostream& operator<<(std::ostream& os, const position_t& x)
    {
        os << x.compilation_unit << ":" << x.line;
        return os;
    }

    std::ostream& operator<<(std::ostream& os, const function_t& x)
    {
        if (x.compilation_unit)
            os << *x.compilation_unit << ":";
        os << x.name;
        return os;
    }

    std::ostream& operator<<(std::ostream& os, const bounds_t::position_range_t& x)
    {
        os << x.first << " - " << x.second;
        return os;
    }

    std::ostream& operator<<(std::ostream& os, const bounds_t& x)
    {
        std::visit(overloaded{
            [&os](const auto& x) { os << x; },
            [&os](std::monostate) { os << "<bounds not set> (monostate)"; }
            }, x._value);
        return os;
    }

    std::ostream& operator<<(std::ostream& os, const method_total_t& x)
    {
        os << "total energy method, short section? " << (x.short_section ? "yes" : "no");
        return os;
    }

    std::ostream& operator<<(std::ostream& os, const method_profile_t& x)
    {
        os << "full profile method, interval: ";
        os << x.interval.count() << "ms";
        os << ", samples: ";
        if (x.samples)
            os << *x.samples;
        else
            os << "n/a";
        return os;
    }

    std::ostream& operator<<(std::ostream& os, const misc_attributes_t& x)
    {
        std::visit(overloaded{
            [&os](const auto& x) { os << x; },
            [&os](std::monostate) { os << "<misc attributes not set> (monostate)"; }
            }, x._value);
        return os;
    }

    std::ostream& operator<<(std::ostream& os, const section_t& x)
    {
        static constexpr auto indent = indentation<section_t>{};
        os << indent << "label: ";
        if (x.label)
            os << *x.label;
        else
            os << "n/a";
        os << "\n" << indent << "extra: ";
        if (x.extra)
            os << *x.extra;
        else
            os << "n/a";
        os << "\n" << indent << "targets: " << x.targets;
        os << "\n" << indent << "bounds: " << x.bounds;
        os << "\n" << indent << "misc: " << x.misc;
        os << "\n" << indent << "allow concurrency? " << (x.allow_concurrency ? "yes" : "no");
        return os;
    }

    std::ostream& operator<<(std::ostream& os, const group_t& x)
    {
        static constexpr auto indent = indentation<group_t>{};
        os << indent << "begin group\n";
        os << indent << "label: ";
        if (x.label)
            os << *x.label;
        else
            os << "n/a";
        os << "\n" << indent << "extra: ";
        if (x.extra)
            os << *x.extra;
        else
            os << "n/a";
        os << "\n";
        for (const auto& sec : x.sections)
        {
            os << indent << "begin section" << "\n";
            os << sec << "\n";
            os << indent << "end section" << "\n";
        }
        os << indent << "end group";
        return os;
    }

    std::ostream& operator<<(std::ostream& os, const config_t& x)
    {
        os << "parameters: ";
        if (x.parameters())
            os << *x.parameters();
        else
            os << "n/a";
        os << "\n" << "groups:";
        for (const auto& g : x.groups())
            os << "\n" << g;
        return os;
    }

    bool operator==(const params_t& lhs, const params_t& rhs)
    {
        return lhs.domain_mask == rhs.domain_mask &&
            lhs.socket_mask == rhs.socket_mask &&
            lhs.device_mask == rhs.device_mask;
    }

    bool operator==(const address_range_t& lhs, const address_range_t& rhs)
    {
        return lhs.start == rhs.start && lhs.end == rhs.end;
    }

    bool operator==(const position_t& lhs, const position_t& rhs)
    {
        return lhs.compilation_unit == rhs.compilation_unit &&
            lhs.line == rhs.line;
    }

    bool operator==(const function_t& lhs, const function_t& rhs)
    {
        return lhs.compilation_unit == rhs.compilation_unit &&
            lhs.name == rhs.name;
    }

    bool operator==(const bounds_t& lhs, const bounds_t& rhs)
    {
        return lhs._value == rhs._value;
    }

    static bool operator==(const method_total_t& lhs, const method_total_t& rhs)
    {
        return lhs.short_section == rhs.short_section;
    }

    static bool operator==(const method_profile_t& lhs, const method_profile_t& rhs)
    {
        return lhs.interval == rhs.interval && lhs.samples == rhs.samples;
    }

    bool operator==(const misc_attributes_t& lhs, const misc_attributes_t& rhs)
    {
        return lhs._value == rhs._value;
    }

    bool operator==(const section_t& lhs, const section_t& rhs)
    {
        return lhs.label == rhs.label &&
            lhs.extra == rhs.extra &&
            lhs.targets == rhs.targets &&
            lhs.bounds == rhs.bounds &&
            lhs.allow_concurrency == rhs.allow_concurrency &&
            lhs.misc == rhs.misc;
    }

    bool operator==(const group_t& lhs, const group_t& rhs)
    {
        return lhs.label == rhs.label &&
            lhs.extra == rhs.extra &&
            lhs.sections == rhs.sections;
    }
}
