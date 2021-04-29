// dbg.cpp

#include <libdwarf/libdwarf.h>

#include "dbg.hpp"
#include "pipe.hpp"

#include <algorithm>
#include <cassert>
#include <cerrno>
#include <charconv>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <locale>
#include <system_error>

#include <expected.hpp>

using namespace tep;

static constexpr const char file_base[] = "/tmp/profiler733978";


// begin helper structs

struct parsed_func
{
    std::string name;
    uintptr_t addr;
    size_t size;
    position pos;
};


// begin helper functions

static std::string get_system_error(int errnum)
{
    char buffer[256];
    return { strerror_r(errnum, buffer, 256) };
}

template<typename... T>
static std::string concat(T&&... args)
{
    std::string result;
    std::string_view views[] = { args... };
    std::string::size_type total_sz = 0;
    for (const auto& v : views)
        total_sz += v.size();
    result.reserve(total_sz);
    for (const auto& v : views)
        result.append(v);
    return result;
}

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

static std::string cu_ambiguous_msg(const std::string& name, const compilation_unit& first,
    const compilation_unit& second)
{
    return concat("Compilation unit ", name,
        " ambiguous; found two matches: '", first.name(),
        "' and '", second.name(), "'");
}

static std::string func_ambiguous_msg(const std::string& name, const function& first,
    const function& second)
{
    return concat("Function ", name,
        " ambiguous; found two matches: '", first.name(),
        "' and '", second.name(), "'");
}

static std::string cu_not_found_msg(const std::string& name)
{
    return concat("Compilation unit '", name, "' not found");
}

static std::string func_not_found_msg(const std::string& name)
{
    return concat("Function '", name, "' not found");
}

static dbg_expected<std::vector<uintptr_t>> get_return_addresses(const char* target)
{
    std::vector<uintptr_t> addresses;
    std::string output = concat(file_base, ".returns");
    cmmn::expected<file_descriptor, pipe_error> fd = file_descriptor::create(
        output.c_str(), fd_flags::write, fd_mode::rdwr_all);
    if (!fd)
        return dbg_error(dbg_error_code::PIPE_ERROR, std::move(fd.error().msg()));

    piped_commands cmd("objdump", "-d", target);
    cmd.add("egrep", "[[:space:]]*[0-9a-f]+:[[:space:]]+c3[[:space:]]+ret")
        .add("awk", "{print $1}")
        .add("sed", "s/:$//");

    pipe_error err = cmd.execute(file_descriptor::std_in, fd.value());
    if (err)
        return dbg_error(dbg_error_code::PIPE_ERROR, std::move(err.msg()));
    fd.value().flush();

    std::ifstream ifs(output);
    if (!ifs)
        return dbg_error(dbg_error_code::SYSTEM_ERROR,
            concat("Could not open ", output, " for reading"));
    for (std::string line; std::getline(ifs, line); )
    {
        uintptr_t addr;
        std::string_view view(line);
        auto [p, ec] = std::from_chars(view.begin(), view.end(), addr, 16);
        std::error_code code = make_error_code(ec);
        if (code)
            return dbg_error(dbg_error_code::FORMAT_ERROR, get_system_error(code.value()));
        addresses.push_back(addr);
    }
    return addresses;
}

static std::vector<std::string_view> split_line(const std::string& line,
    const std::string_view& delim)
{
    std::vector<std::string_view> tokens;
    std::string::size_type current = 0;
    std::string::size_type next;
    while ((next = line.find_first_of(delim, current)) != std::string::npos)
    {
        tokens.emplace_back(&line[current], next - current);
        current = next + delim.length();
    }
    if (current < line.length())
        tokens.emplace_back(&line[current], line.length() - current);
    return tokens;
}

static dbg_expected<std::vector<parsed_func>> get_functions(const char* target)
{
    std::vector<parsed_func> funcs;
    std::string output = concat(file_base, ".syms");
    cmmn::expected<file_descriptor, pipe_error> fd = file_descriptor::create(
        output.c_str(), fd_flags::write, fd_mode::rdwr_all);
    if (!fd)
        return dbg_error(dbg_error_code::PIPE_ERROR, std::move(fd.error().msg()));

    constexpr const size_t token_count = 5;
    piped_commands cmd("nm", "-lSC", "--defined-only", "--format=posix", target);
    cmd.add("sed", "-nE", "s/(.+) T ([0-9a-f]+) ([0-9a-f]+)\\t(.+):([1-9]+)/\\1|\\2|\\3|\\4|\\5/p");

    pipe_error err = cmd.execute(file_descriptor::std_in, fd.value());
    if (err)
        return dbg_error(dbg_error_code::PIPE_ERROR, std::move(err.msg()));
    fd.value().flush();

    std::ifstream ifs(output);
    if (!ifs)
        return dbg_error(dbg_error_code::SYSTEM_ERROR,
            concat("Could not open ", output, " for reading"));
    for (std::string line; std::getline(ifs, line); )
    {
        std::vector<std::string_view> views = split_line(line, "|");
        if (views.size() < token_count)
            return dbg_error(dbg_error_code::FORMAT_ERROR, "invalid format for function parsing");

        uintptr_t addr;
        std::from_chars_result res = std::from_chars(views[1].begin(), views[1].end(), addr, 16);
        std::error_code code = make_error_code(res.ec);
        if (code)
            return dbg_error(dbg_error_code::FORMAT_ERROR, get_system_error(code.value()));

        size_t size;
        res = std::from_chars(views[2].begin(), views[2].end(), size, 16);
        code = make_error_code(res.ec);
        if (code)
            return dbg_error(dbg_error_code::FORMAT_ERROR, get_system_error(code.value()));

        uint32_t lineno;
        res = std::from_chars(views[4].begin(), views[4].end(), lineno, 10);
        code = make_error_code(res.ec);
        if (code)
            return dbg_error(dbg_error_code::FORMAT_ERROR, get_system_error(code.value()));

        funcs.push_back({ remove_spaces(views[0]), addr, size,
        position(std::string(views[3]), lineno) });
    }
    return funcs;
}


// end helper functions


// dbg_error

dbg_error::dbg_error(dbg_error_code c, const char* msg) :
    code(c), message(msg)
{}

dbg_error::dbg_error(dbg_error_code c, const std::string& msg) :
    code(c), message(msg)
{}

dbg_error::dbg_error(dbg_error_code c, std::string&& msg) :
    code(c), message(std::move(msg))
{}

dbg_error dbg_error::success()
{
    return { dbg_error_code::SUCCESS, "No error" };
}

dbg_error::operator bool() const
{
    return code != dbg_error_code::SUCCESS;
}


// position

position::position(const std::string& cu, uint32_t line) :
    _cu(cu),
    _line(line)
{}


position::position(std::string&& cu, uint32_t line) :
    _cu(std::move(cu)),
    _line(line)
{}

position::position(const char* cu, uint32_t line) :
    _cu(cu),
    _line(line)
{
    assert(cu != nullptr);
}

const std::string& position::cu() const
{
    return _cu;
}

uint32_t position::line() const
{
    return _line;
}

bool position::contains(const std::string& cu) const
{
    if (cu.empty())
        return true;
    // existing path is always absolute
    std::filesystem::path path(_cu);
    std::filesystem::path sub(cu);
    if (path == sub)
        return true;
    if (std::search(path.begin(), path.end(), sub.begin(), sub.end()) != path.end())
        return true;
    return false;
}


// function_bounds

function_bounds::function_bounds(uintptr_t start, const std::vector<uintptr_t>& rets) :
    _start(start),
    _rets(rets)
{}

function_bounds::function_bounds(uintptr_t start, std::vector<uintptr_t>&& rets) :
    _start(start),
    _rets(std::move(rets))
{}

uintptr_t function_bounds::start() const
{
    return _start;
}

const std::vector<uintptr_t>& function_bounds::returns() const
{
    return _rets;
}


// function

function::function(const std::string& name, const position& pos, const function_bounds& bounds) :
    _name(name),
    _pos(pos),
    _bounds(bounds)
{}

function::function(const std::string& name, const position& pos, function_bounds&& bounds) :
    _name(name),
    _pos(pos),
    _bounds(std::move(bounds))
{}

function::function(const std::string& name, position&& pos, const function_bounds& bounds) :
    _name(name),
    _pos(std::move(pos)),
    _bounds(bounds)
{}

function::function(const std::string& name, position&& pos, function_bounds&& bounds) :
    _name(name),
    _pos(std::move(pos)),
    _bounds(std::move(bounds))
{}

function::function(std::string&& name, const position& pos, const function_bounds& bounds) :
    _name(std::move(name)),
    _pos(pos),
    _bounds(bounds)
{}

function::function(std::string&& name, const position& pos, function_bounds&& bounds) :
    _name(std::move(name)),
    _pos(pos),
    _bounds(std::move(bounds))
{}

function::function(std::string&& name, position&& pos, const function_bounds& bounds) :
    _name(std::move(name)),
    _pos(std::move(pos)),
    _bounds(bounds)
{}

function::function(std::string&& name, position&& pos, function_bounds&& bounds) :
    _name(std::move(name)),
    _pos(std::move(pos)),
    _bounds(std::move(bounds))
{}

const std::string& function::name() const
{
    return _name;
}

const position& function::pos() const
{
    return _pos;
}

const function_bounds& function::bounds() const
{
    return _bounds;
}

bool function::matches(const std::string& name, const std::string& cu) const
{
    std::string_view view(_name.data(), _name.size());
    std::string_view start(name.data(), name.size());
    return view.substr(0, start.size()) == start && _pos.contains(cu);
}


// compilation_unit

compilation_unit::compilation_unit(const char* name) :
    _name(name),
    _lines()
{}

compilation_unit::compilation_unit(const std::string& name) :
    _name(name),
    _lines()
{}

compilation_unit::compilation_unit(std::string&& name) :
    _name(std::move(name)),
    _lines()
{}

void compilation_unit::add_address(uint32_t lineno, uintptr_t lineaddr)
{
    _lines[lineno].push_back(lineaddr);
}

dbg_expected<uintptr_t> compilation_unit::line_first_addr(uint32_t lineno) const
{
    return line_addr(lineno, 0);
}

dbg_expected<uintptr_t> compilation_unit::line_addr(uint32_t lineno, size_t order) const
{
    assert(order < _lines.size());
    for (const auto& [no, addrs] : _lines)
        if (no >= lineno)
            return addrs[order];
    return dbg_error(dbg_error_code::INVALID_LINE, "Invalid line");
}


// begin dbg_line_info

dbg_expected<dbg_line_info> dbg_line_info::create(const char* filename)
{
    dbg_error error(dbg_error::success());
    dbg_line_info dli(filename, error);
    if (error)
        return error;
    return dli;
}

dbg_line_info::dbg_line_info(const char* filename, dbg_error& err) :
    _units(),
    _funcs()
{
    assert(filename != nullptr);
    FILE* img = fopen(filename, "r");
    if (img == NULL)
    {
        err = { dbg_error_code::SYSTEM_ERROR, get_system_error(errno) };
        return;
    }

    dbg_error error = get_line_info(fileno(img));
    if (error)
        err = std::move(error);
    if (fclose(img) != 0)
    {
        err = { dbg_error_code::SYSTEM_ERROR, get_system_error(errno) };
        return;
    }

    error = get_functions(filename);
    if (error)
        err = std::move(error);
}

bool dbg_line_info::has_dbg_symbols() const
{
    return !_units.empty();
}

dbg_expected<const compilation_unit*> dbg_line_info::find_cu(const std::string& name) const
{
    return find_cu(name.c_str());
}

dbg_expected<const compilation_unit*> dbg_line_info::find_cu(const char* name) const
{
    return find_cu(name);
}

dbg_expected<compilation_unit*> dbg_line_info::find_cu(const std::string& name)
{
    return find_cu(name.c_str());
}

dbg_expected<compilation_unit*> dbg_line_info::find_cu(const char* name)
{
    compilation_unit* contains = nullptr;
    for (auto& cu : _units)
    {
        // existing path is always absolute
        std::filesystem::path existing_path(cu.name());
        std::filesystem::path subpath(name);

        if (existing_path == subpath)
            return &cu;
        // check if name is a subpath of an existing CU path
        else if (std::search(existing_path.begin(), existing_path.end(),
            subpath.begin(), subpath.end()) != existing_path.end())
        {
            if (contains != nullptr)
                return dbg_error(dbg_error_code::COMPILATION_UNIT_AMBIGUOUS,
                    cu_ambiguous_msg(name, *contains, cu));
            contains = &cu;
        }
    }
    if (contains == nullptr)
        return dbg_error(dbg_error_code::COMPILATION_UNIT_NOT_FOUND,
            cu_not_found_msg(name));
    return contains;
}

dbg_expected<const function*> dbg_line_info::find_function(const std::string& name,
    const std::string& cu) const
{
    const function* match = nullptr;
    for (const auto& f : _funcs)
    {
        if (f.matches(remove_spaces(name), cu))
        {
            if (match != nullptr)
                return dbg_error(dbg_error_code::FUNCTION_AMBIGUOUS,
                    func_ambiguous_msg(name, f, *match));
            match = &f;
        }
    }
    if (match == nullptr)
        return dbg_error(dbg_error_code::FUNCTION_NOT_FOUND, func_not_found_msg(name));
    return match;
}


// TODO: need to rewrite this using newer functions
dbg_error dbg_line_info::get_line_info(int fd)
{
    int rv;
    Dwarf_Debug dw_dbg;
    Dwarf_Error dw_err;
    if ((rv = dwarf_init(fd, DW_DLC_READ, NULL, NULL, &dw_dbg, &dw_err)) != DW_DLV_OK)
    {
        if (rv == DW_DLV_NO_ENTRY)
            return { dbg_error_code::DEBUG_SYMBOLS_NOT_FOUND, dwarf_errmsg(dw_err) };
        if (rv == DW_DLV_ERROR)
            return { dbg_error_code::DWARF_ERROR, dwarf_errmsg(dw_err) };
    }

    // iterate all compilation units
    while (true)
    {
        Dwarf_Unsigned next_cu_size;
        Dwarf_Die dw_die;
        Dwarf_Line* linebuf;
        Dwarf_Signed linecount;
        if ((rv = dwarf_next_cu_header(dw_dbg, NULL, NULL, NULL, NULL, &next_cu_size, &dw_err)) !=
            DW_DLV_OK)
        {
            // if no more compilation units left
            if (rv == DW_DLV_NO_ENTRY)
                break;
            return { dbg_error_code::DWARF_ERROR, dwarf_errmsg(dw_err) };
        }
        if ((rv = dwarf_siblingof(dw_dbg, NULL, &dw_die, &dw_err)) != DW_DLV_OK)
            return { dbg_error_code::DWARF_ERROR, dwarf_errmsg(dw_err) };
        if ((rv = dwarf_srclines(dw_die, &linebuf, &linecount, &dw_err)) != DW_DLV_OK)
            return { dbg_error_code::DWARF_ERROR, dwarf_errmsg(dw_err) };

        for (Dwarf_Signed ix = 0; ix < linecount; ix++)
        {
            char* srcfile;
            Dwarf_Unsigned lineno;
            Dwarf_Addr lineaddr;

            if ((rv = dwarf_linesrc(linebuf[ix], &srcfile, &dw_err)) != DW_DLV_OK)
                return { dbg_error_code::DWARF_ERROR, dwarf_errmsg(dw_err) };
            if ((rv = dwarf_lineno(linebuf[ix], &lineno, &dw_err)) != DW_DLV_OK)
                return { dbg_error_code::DWARF_ERROR, dwarf_errmsg(dw_err) };
            if ((rv = dwarf_lineaddr(linebuf[ix], &lineaddr, &dw_err)) != DW_DLV_OK)
                return { dbg_error_code::DWARF_ERROR, dwarf_errmsg(dw_err) };

            dbg_expected<compilation_unit*> cu = find_cu(srcfile);
            if (!cu)
            {
                if (cu.error().code == dbg_error_code::COMPILATION_UNIT_NOT_FOUND)
                    _units.emplace_back(srcfile).add_address(lineno, lineaddr);
                // should never happen since we're comparing absolute paths
                else if (cu.error().code == dbg_error_code::COMPILATION_UNIT_AMBIGUOUS)
                    return std::move(cu.error());
                else // should never happen
                    assert(false);
            }
            else
            {
                cu.value()->add_address(lineno, lineaddr);
            }

            dwarf_dealloc(dw_dbg, linebuf[ix], DW_DLA_LINE);
            dwarf_dealloc(dw_dbg, srcfile, DW_DLA_STRING);
        }
        dwarf_dealloc(dw_dbg, linebuf, DW_DLA_LIST);
    }

    if (dwarf_finish(dw_dbg, &dw_err) != DW_DLV_OK)
        return { dbg_error_code::DWARF_ERROR, dwarf_errmsg(dw_err) };
    return dbg_error::success();
}

dbg_error dbg_line_info::get_functions(const char* filename)
{
    dbg_expected<std::vector<uintptr_t>> returns = get_return_addresses(filename);
    if (!returns)
        return std::move(returns.error());
    dbg_expected<std::vector<parsed_func>> funcs = ::get_functions(filename);
    if (!funcs)
        return std::move(returns.error());

    for (auto& pf : funcs.value())
    {
        std::vector<uintptr_t> effrets;
        for (uintptr_t ret : returns.value())
        {
            if (ret > pf.addr && ret < pf.addr + pf.size)
                effrets.push_back(ret);
        }
        _funcs.emplace_back(std::move(pf.name), std::move(pf.pos),
            function_bounds(pf.addr, std::move(effrets)));
    }
    return dbg_error::success();
}


// operator overloads

std::ostream& tep::operator<<(std::ostream& os, const dbg_error& de)
{
    os << de.message << " (error code "
        << static_cast<std::underlying_type_t<dbg_error_code>>(de.code) << ")";
    return os;
}

std::ostream& tep::operator<<(std::ostream& os, const position& p)
{
    os << p.cu() << ":" << p.line();
    return os;
}

std::ostream& tep::operator<<(std::ostream& os, const function_bounds& fb)
{
    std::ios::fmtflags os_flags(os.flags());
    os << std::hex << "[0x" << fb.start() << "] -";
    for (uintptr_t ret : fb.returns())
        os << " 0x" << ret;
    os.flags(os_flags);
    return os;
}

std::ostream& tep::operator<<(std::ostream& os, const function& f)
{
    os << f.name() << " @ " << f.pos() << ": " << f.bounds();
    return os;
}

std::ostream& tep::operator<<(std::ostream& os, const compilation_unit& cu)
{
    for (const auto& [no, addrs] : cu._lines)
    {
        std::ios::fmtflags os_flags(os.flags());
        os << cu._name << ":" << no << " @" << std::hex;
        for (const auto& addr : addrs)
            os << " 0x" << addr;
        os.flags(os_flags);
        os << "\n";
    }
    return os;
}

std::ostream& tep::operator<<(std::ostream& os, const dbg_line_info& dbg_info)
{
    for (const auto& cu : dbg_info._units)
        os << cu;
    os << "\n";
    for (const auto& f : dbg_info._funcs)
        os << f << "\n";
    return os;
}


bool tep::operator==(const compilation_unit& lhs, const compilation_unit& rhs)
{
    return lhs.name() == rhs.name();
}
