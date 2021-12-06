// dbg.cpp

#ifdef TEP_USE_LIBDWARF
#include <libdwarf/libdwarf.h>
#endif

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

#include <nonstd/expected.hpp>
#include <util/concat.hpp>

using namespace tep;

namespace
{
    constexpr const char file_base[] = "/tmp/profiler733978";
    constexpr const char et_dyn[] = "DYN";
    constexpr const char et_exec[] = "EXEC";

#if defined(__x86_64__) || defined(__i386__)
    constexpr const char ret_match[] = "[[:space:]]*[0-9a-f]+:[[:space:]]+c3[[:space:]]+ret";
#elif defined(__powerpc64__)
    constexpr const char ret_match[] = "blr[[:space:]]*$"; // TODO: review
#else
    constexpr const char ret_match[] = "";
#endif // __x86_64__ || __i386__

    struct parsed_func
    {
        std::string name;
        std::string suffix;
        uintptr_t addr;
        size_t size;
        position pos;
    };

    std::string get_system_error(int errnum)
    {
        char buffer[256];
        return { strerror_r(errnum, buffer, 256) };
    }

    template<typename Ret>
    struct pipe_error_func
    {
        Ret operator()(pipe_error&& err)
        {
            return Ret(nonstd::unexpect,
                dbg_error_code::PIPE_ERROR, std::move(err.msg()));
        }
    };

    template<typename Ret>
    struct format_error_func
    {
        Ret operator()(const std::error_code& ec)
        {
            return Ret(nonstd::unexpect,
                dbg_error_code::FORMAT_ERROR,
                cmmn::concat("Format error: ", get_system_error(ec.value()))
            );
        }
    };

    template<typename Ret>
    struct system_error_func
    {
        Ret operator()(std::string&& prefix)
        {
            return Ret(nonstd::unexpect,
                dbg_error_code::SYSTEM_ERROR,
                cmmn::concat(prefix, ": ", get_system_error(errno))
            );
        }
    };

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

    dbg_error cu_ambiguous(const std::string& name, const unit_lines& first,
        const unit_lines& second)
    {
        return dbg_error(dbg_error_code::COMPILATION_UNIT_AMBIGUOUS,
            cmmn::concat("Compilation unit ", name, " ambiguous; found two matches: '",
                first.name(), "' and '", second.name(), "'"));
    }

    dbg_error func_ambiguous(const std::string& name, const function& first,
        const function& second)
    {
        return dbg_error(dbg_error_code::FUNCTION_AMBIGUOUS,
            cmmn::concat("Function ", name, " ambiguous; found two matches: '",
                first.name(), "' and '", second.name(), "'"));
    }

    template<typename Iterator>
    dbg_error func_ambiguous(const std::string& name, Iterator begin, Iterator end)
    {
        std::string msg = cmmn::concat("Function '", name, "' ambiguous; found matches:");
        for (auto it = begin; it != end; it++)
            msg.append(" '").append((*it)->name()).append("'");
        return dbg_error(dbg_error_code::FUNCTION_AMBIGUOUS, std::move(msg));
    }

    dbg_error cu_not_found(const std::string& name)
    {
        return dbg_error(dbg_error_code::COMPILATION_UNIT_NOT_FOUND,
            cmmn::concat("Compilation unit '", name, "' not found"));
    }

    dbg_error func_not_found(const std::string& name)
    {
        return dbg_error(dbg_error_code::FUNCTION_NOT_FOUND,
            cmmn::concat("Function '", name, "' not found"));
    }

    dbg_expected<std::vector<uintptr_t>> get_return_addresses(const char* target)
    {
        using rettype = dbg_expected<std::vector<uintptr_t>>;
        auto pipe_err = pipe_error_func<rettype>{};
        auto sys_err = system_error_func<rettype>{};
        auto fmt_err = format_error_func<rettype>{};

        std::vector<uintptr_t> addresses;
        std::string output = cmmn::concat(file_base, ".returns");
        auto fd = file_descriptor::create(output.c_str(), fd_flags::write, fd_mode::rdwr_all);
        if (!fd)
            return pipe_err(std::move(fd.error()));

        piped_commands cmd("objdump", "-d", target);
        cmd.add("egrep", ret_match)
            .add("awk", "{print $1}")
            .add("sed", "s/:$//");

        if (auto err = cmd.execute(file_descriptor::std_in, *fd))
            return pipe_err(std::move(err));
        fd->flush();

        std::ifstream ifs(output);
        if (!ifs)
            return sys_err(cmmn::concat("Could not open ", output));
        for (std::string line; std::getline(ifs, line); )
        {
            uintptr_t addr;
            std::string_view view(line);
            auto [p, ec] = std::from_chars(view.begin(), view.end(), addr, 16);
            std::error_code code = make_error_code(ec);
            if (code)
                return fmt_err(code);
            addresses.push_back(addr);
        }
        return addresses;
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

#if defined(__x86_64__) || defined(__i386__)

    dbg_expected<size_t>
        get_function_entry_offset(std::string_view, std::string_view)
    {
        return 0;
    }

#elif defined(__powerpc64__)

    // obtains the offset between the function's global and local entry points, in bytes
    ssize_t local_entry_point_offset(uint8_t st_other)
    {
        constexpr static const size_t ins_size = 4;
        // get the three most significant bits
        st_other = st_other >> 5;
        switch (st_other)
        {
        case 0:
        case 1:
            return 0;
        case 2:
            return ins_size;
        case 3:
            return 2 * ins_size;
        case 4:
            return 4 * ins_size;
        case 5:
            return 8 * ins_size;
        case 6:
            return 16 * ins_size;
        default:
            break;
        }
        assert(false);
        return -1;
    }

    // account for the local function entry offset defined in the OpenPOWER ABI
    dbg_expected<size_t>
        get_function_entry_offset(std::string_view target, std::string_view func_name)
    {
        using rettype = dbg_expected<size_t>;
        auto pipe_err = pipe_error_func<rettype>{};
        auto sys_err = system_error_func<rettype>{};
        auto fmt_err = format_error_func<rettype>{};

        std::string output = cmmn::concat(file_base, ".st_other");
        auto fd = file_descriptor::create(output.c_str(), fd_flags::write, fd_mode::rdwr_all);
        if (!fd)
            return pipe_err(std::move(fd.error()));

        piped_commands cmd("objdump", "-tCw", std::string(target));
        cmd.add("grep", "-F", std::string(func_name));
        cmd.add("sed", "-nE", "s/.*0x([0-9a-f][0-9a-f]).*/\\1/p");
        if (pipe_error err = cmd.execute(file_descriptor::std_in, *fd))
            return pipe_err(std::move(err));

        std::ifstream ifs(output);
        if (!ifs)
            return sys_err(cmmn::concat("Could not open ", output));

        ssize_t offset = 0;
        std::string line;
        std::getline(ifs, line);
        if (line.empty())
            return offset;

        uint8_t st_other = 0;
        std::from_chars_result res = std::from_chars(line.data(), line.data() + line.size(),
            st_other, 16);
        if (std::error_code code = make_error_code(res.ec))
            return fmt_err(code);
        offset = local_entry_point_offset(st_other);
        if (offset < 0)
            return rettype(nonstd::unexpect,
                dbg_error_code::FORMAT_ERROR,
                "Incorrect value of symbol's st_other field");
        return offset;
    }

#endif

    dbg_expected<std::vector<parsed_func>> get_functions(const char* target)
    {
        using rettype = dbg_expected<std::vector<parsed_func>>;
        auto pipe_err = pipe_error_func<rettype>{};
        auto sys_err = system_error_func<rettype>{};
        auto fmt_err = format_error_func<rettype>{};

        std::vector<parsed_func> funcs;
        std::string output = cmmn::concat(file_base, ".syms");
        auto fd = file_descriptor::create(
            output.c_str(), fd_flags::write, fd_mode::rdwr_all);
        if (!fd)
            return pipe_err(std::move(fd.error()));

        constexpr const size_t token_count = 6;
        constexpr const size_t iname = 0;
        constexpr const size_t isuff = 1;
        constexpr const size_t iaddr = 2;
        constexpr const size_t isize = 3;
        constexpr const size_t ifile = 4;
        constexpr const size_t iline = 5;

        piped_commands cmd("nm", "-lSC", "--defined-only", "--format=posix", target);
        cmd.add("sed", "-nE",
            "-e", "s/(.+) (\\[.+\\]) [tTW] ([0-9a-f]+) ([0-9a-f]+)\\t(.+):([1-9]+)/\\1|\\2|\\3|\\4|\\5|\\6/p",
            "-e", "s/(.+) [tTW] ([0-9a-f]+) ([0-9a-f]+)\\t(.+):([1-9]+)/\\1||\\2|\\3|\\4|\\5/p"
        );

        if (auto err = cmd.execute(file_descriptor::std_in, *fd))
            return pipe_err(std::move(err));
        fd->flush();

        std::ifstream ifs(output);
        if (!ifs)
            return sys_err(cmmn::concat("Could not open ", output));
        for (std::string line; std::getline(ifs, line); )
        {
            std::vector<std::string_view> views = split_line(line, "|");
            assert(views.size() == token_count);
            if (views.size() != token_count)
                return rettype(nonstd::unexpect,
                    dbg_error_code::FORMAT_ERROR, "Invalid format for function parsing");

            uintptr_t addr;
            auto res = std::from_chars(views[iaddr].begin(), views[iaddr].end(), addr, 16);
            if (std::error_code code = make_error_code(res.ec))
                return fmt_err(code);

            size_t size;
            res = std::from_chars(views[isize].begin(), views[isize].end(), size, 16);
            if (std::error_code code = make_error_code(res.ec))
                return fmt_err(code);

            uint32_t lineno;
            res = std::from_chars(views[iline].begin(), views[iline].end(), lineno, 10);
            if (std::error_code code = make_error_code(res.ec))
                return fmt_err(code);

            auto localentry_offset = get_function_entry_offset(target, views[iname]);
            if (!localentry_offset)
                return rettype(nonstd::unexpect, std::move(localentry_offset.error()));
            addr += *localentry_offset;
            size -= *localentry_offset;

            funcs.push_back(
                {
                    std::string(views[iname]),
                    std::string(views[isuff]),
                    addr,
                    size,
                    position(std::string(views[ifile]), lineno)
                }
            );
        }
        return funcs;
    }

    dbg_expected<bool> has_debug_info(const char* target)
    {
        assert(target);
        using rettype = dbg_expected<bool>;
        auto pipe_err = pipe_error_func<rettype>{};
        auto sys_err = system_error_func<rettype>{};

        std::string output = cmmn::concat(file_base, ".dbg");
        auto fd = file_descriptor::create(output.c_str(), fd_flags::write, fd_mode::rdwr_all);
        if (!fd)
            return pipe_err(std::move(fd.error()));

        piped_commands cmd("objdump", "-h", target);
        cmd.add("sed", "-nE", "s/.*(debug_info).*/\\1/p");

        if (auto err = cmd.execute(file_descriptor::std_in, *fd))
            return pipe_err(std::move(err));
        fd->flush();

        std::ifstream ifs(output);
        if (!ifs)
            return sys_err(cmmn::concat("Could not open ", output));

        std::string line;
        std::getline(ifs, line);
        return !line.empty();
    }
}

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

std::string function::name() const
{
    if (_suffix.empty())
        return _name;
    return cmmn::concat(_name, " ", _suffix);
}

const std::string& function::prototype() const
{
    return _name;
}

const std::string& function::suffix() const
{
    return _suffix;
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
    std::string nospaces = remove_spaces(this->name());
    std::string to_match = remove_spaces(name);
    std::string_view view(nospaces);
    std::string_view match_view(to_match);
    // this function's name starts with the name to match
    return view.substr(0, match_view.size()) == match_view && _pos.contains(cu);
}

bool function::equals(const std::string& name, const std::string& cu) const
{
    return remove_spaces(this->name()) == remove_spaces(name) && _pos.contains(cu);
}


// unit_lines

unit_lines::unit_lines(const char* name) :
    _name(name),
    _lines()
{}

unit_lines::unit_lines(const std::string& name) :
    _name(name),
    _lines()
{}

unit_lines::unit_lines(std::string&& name) :
    _name(std::move(name)),
    _lines()
{}

void unit_lines::add_address(uint32_t lineno, uintptr_t lineaddr)
{
    _lines[lineno].insert(lineaddr);
}

const std::string& unit_lines::name() const
{
    return _name;
}

dbg_expected<std::pair<uint32_t, uintptr_t>>
unit_lines::lowest_addr(uint32_t lineno) const
{
    return get_addr_impl(lineno, &unit_lines::get_lowest_addr);
}

dbg_expected<std::pair<uint32_t, uintptr_t>>
unit_lines::highest_addr(uint32_t lineno) const
{
    return get_addr_impl(lineno, &unit_lines::get_highest_addr);
}

#define CALL_MEMBER_FN(object, member_ptr) ((object).*(member_ptr))

dbg_expected<std::pair<uint32_t, uintptr_t>>
unit_lines::get_addr_impl(uint32_t lineno, addr_getter fn) const
{
    using pair = std::pair<uint32_t, uintptr_t>;
    using rettype = dbg_expected<pair>;

    using it = decltype(_lines)::const_iterator;
    std::pair<it, it> eqrange = _lines.equal_range(lineno);
    // if there is an element not less than lineno
    if (eqrange.first != _lines.end())
    {
        auto res = CALL_MEMBER_FN(*this, fn)(eqrange.first->second);
        if (!res)
            return rettype(nonstd::unexpect, std::move(res.error()));
        return pair{ eqrange.first->first, *res };
    }
    // otherwise, the first element greater than lineno
    if (eqrange.second != _lines.end())
    {
        auto res = CALL_MEMBER_FN(*this, fn)(eqrange.second->second);
        if (!res)
            return rettype(nonstd::unexpect, std::move(res.error()));
        return pair{ eqrange.second->first, *res };
    }
    return rettype(nonstd::unexpect,
        dbg_error_code::INVALID_LINE, "Invalid line");
}

dbg_expected<uintptr_t> unit_lines::get_lowest_addr(
    const decltype(_lines)::mapped_type& addrs) const
{
    assert(!addrs.empty());
    using rettype = dbg_expected<uintptr_t>;
    if (addrs.empty())
        return rettype(nonstd::unexpect, dbg_error_code::INVALID_LINE, "Line has no addresses");
    return *addrs.begin();
}

dbg_expected<uintptr_t> unit_lines::get_highest_addr(
    const decltype(_lines)::mapped_type& addrs) const
{
    assert(!addrs.empty());
    using rettype = dbg_expected<uintptr_t>;
    if (addrs.empty())
        return rettype(nonstd::unexpect, dbg_error_code::INVALID_LINE, "Line has no addresses");
    return *addrs.rbegin();
}


// header_info

header_info::header_info(const char* target, dbg_error& err) :
    _exectype(type::unknown)
{
    err = get_exec_type(target);
}

header_info::type header_info::exec_type() const
{
    return _exectype;
}

dbg_error header_info::get_exec_type(const char* target)
{
    assert(target != nullptr);
    std::string output = cmmn::concat(file_base, ".type");
    auto fd = file_descriptor::create(output.c_str(), fd_flags::write, fd_mode::rdwr_all);
    if (!fd)
        return dbg_error(dbg_error_code::PIPE_ERROR, std::move(fd.error().msg()));

    piped_commands cmd("readelf", "-h", target);
    cmd.add("sed", "-nE", "s/.*Type:[[:space:]]+([A-Z]+).*/\\1/p");

    pipe_error err = cmd.execute(file_descriptor::std_in, *fd);
    if (err)
        return dbg_error(dbg_error_code::PIPE_ERROR, std::move(err.msg()));
    fd->flush();

    std::ifstream ifs(output);
    if (!ifs)
        return dbg_error(dbg_error_code::SYSTEM_ERROR,
            cmmn::concat("Could not open ", output, " for reading"));

    std::string line;
    if (!std::getline(ifs, line))
        return dbg_error(dbg_error_code::FORMAT_ERROR, cmmn::concat("Expected a line in ", output));

    if (line == et_dyn)
        _exectype = type::dyn;
    else if (line == et_exec)
        _exectype = type::exec;
    else
        return dbg_error(dbg_error_code::UNKNOWN_TARGET_TYPE,
            cmmn::concat("Target ", target, " not of type ", et_dyn, " or ", et_exec));

    assert(_exectype != type::unknown);
    return dbg_error::success();
}


// begin dbg_info

dbg_expected<dbg_info> dbg_info::create(const char* filename)
{
    dbg_error error(dbg_error::success());
    dbg_info dli(filename, error);
    if (error)
        return dbg_expected<dbg_info>(nonstd::unexpect, std::move(error));
    return dli;
}

dbg_expected<dbg_info> dbg_info::create(const std::string& filename)
{
    return create(filename.c_str());
}

dbg_info::dbg_info(const char* filename, dbg_error& err) :
    _hi(filename, err),
    _lines(),
    _funcs()
{
    assert(filename != nullptr);
    if (err)
        return;

    auto di = has_debug_info(filename);
    if (!di)
    {
        err = std::move(di.error());
        return;
    }
    if (!*di)
    {
        err = { dbg_error_code::DEBUG_SYMBOLS_NOT_FOUND, "No debugging information found" };
        return;
    }

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

bool dbg_info::has_line_info() const
{
    return !_lines.empty();
}

const header_info& dbg_info::header() const
{
    return _hi;
}

dbg_expected<const unit_lines*> dbg_info::find_lines(const std::string& name) const
{
    return find_lines(name.c_str());
}

dbg_expected<const unit_lines*> dbg_info::find_lines(const char* name) const
{
    return find_lines_impl(*this, name);
}

dbg_expected<unit_lines*> dbg_info::find_lines(const std::string& name)
{
    return find_lines(name.c_str());
}

dbg_expected<unit_lines*> dbg_info::find_lines(const char* name)
{
    return find_lines_impl(*this, name);
}

dbg_expected<const function*> dbg_info::find_function(
    const std::string& name,
    const std::string& cu) const
{
    using rettype = dbg_expected<const function*>;

    std::vector<const function*> matches;
    // first, iterate all functions and find matched candidates
    for (const function& f : _funcs)
        if (f.matches(name, cu))
            matches.push_back(&f);
    // no matches found means the function does not exist
    if (matches.empty())
        return rettype(nonstd::unexpect, func_not_found(name));

    // next, iterate all matches and check if any of them equal the function we are
    // searching for
    const function* retval = nullptr;
    for (const function* fptr : matches)
    {
        if (fptr->equals(name, cu))
        {
            // another equal function was previously located
            if (retval)
                return rettype(nonstd::unexpect, func_ambiguous(name, *retval, *fptr));
            retval = fptr;
        }
    }
    // if no equal function found
    if (!retval)
    {
        // if more than one match, disambiguate using suffix
        if (matches.size() > 1)
        {
            auto it = std::partition(matches.begin(), matches.end(), [](const function* f)
                {
                    return !f->suffix().empty();
                });
            if (std::distance(it, matches.end()) != 1)
                return rettype(nonstd::unexpect,
                    func_ambiguous(name, matches.begin(), matches.end()));
            return *it;
        }
        else
            retval = matches.front();
    }

    assert(retval);
    return retval;
}

template<typename T>
auto dbg_info::find_lines_impl(T& instance, const char* name)
-> decltype(instance.find_lines(name))
{
    assert(name);
    using rettype = decltype(instance.find_lines(name));
    using value_type = decltype(*instance.find_lines(name));

    value_type contains = nullptr;
    for (auto& cu : instance._lines)
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
                return rettype(nonstd::unexpect, cu_ambiguous(name, *contains, cu));
            contains = &cu;
        }
    }
    if (contains == nullptr)
        return rettype(nonstd::unexpect, cu_not_found(name));
    return contains;
}

// TODO: need to rewrite this using newer functions
#ifdef TEP_USE_LIBDWARF

dbg_error dbg_info::get_line_info(int fd)
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

            dbg_expected<unit_lines*> cu = find_lines(srcfile);
            if (!cu)
            {
                if (cu.error().code == dbg_error_code::COMPILATION_UNIT_NOT_FOUND)
                    _lines.emplace_back(srcfile).add_address(lineno, lineaddr);
                // should never happen since we're comparing absolute paths
                else if (cu.error().code == dbg_error_code::COMPILATION_UNIT_AMBIGUOUS)
                    return std::move(cu.error());
                else // should never happen
                    assert(false);
            }
            else
            {
                (*cu)->add_address(lineno, lineaddr);
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

#else

dbg_error dbg_info::get_line_info(int)
{
    return dbg_error::success();
}

#endif // TEP_USE_LIBDWARF

dbg_error dbg_info::get_functions(const char* filename)
{
    dbg_expected<std::vector<uintptr_t>> returns = get_return_addresses(filename);
    if (!returns)
        return std::move(returns.error());
    dbg_expected<std::vector<parsed_func>> funcs = ::get_functions(filename);
    if (!funcs)
        return std::move(funcs.error());

    for (auto& pf : *funcs)
    {
        std::vector<uintptr_t> effrets;
        for (uintptr_t ret : *returns)
        {
            if (ret > pf.addr && ret < pf.addr + pf.size)
                effrets.push_back(ret);
        }
        _funcs.emplace_back(
            std::move(pf.name),
            std::move(pf.suffix),
            std::move(pf.pos),
            function_bounds(pf.addr, std::move(effrets))
        );
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

std::ostream& tep::operator<<(std::ostream& os, const unit_lines& cu)
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

std::ostream& tep::operator<<(std::ostream& os, const header_info& hi)
{
    os << "Target type: ";
    switch (hi.exec_type())
    {
    case header_info::type::dyn:
        os << et_dyn;
        break;
    case header_info::type::exec:
        os << et_exec;
        break;
    default:
        assert(false);
    }
    return os;
}

std::ostream& tep::operator<<(std::ostream& os, const dbg_info& dbg_info)
{
    os << dbg_info.header() << "\n";
    for (const auto& l : dbg_info._lines)
        os << l;
    for (const auto& f : dbg_info._funcs)
        os << f << "\n";
    return os;
}


bool tep::operator==(const unit_lines& lhs, const unit_lines& rhs)
{
    return lhs.name() == rhs.name();
}
