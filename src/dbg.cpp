// dbg.cpp

#include <libdwarf/libdwarf.h>

#include "dbg.hpp"

#include <algorithm>
#include <iostream>
#include <iomanip>
#include <cassert>
#include <cstring>
#include <cerrno>
#include <filesystem>

using namespace tep;


// begin helper functions


std::string get_system_error()
{
    char buffer[256];
    return { strerror_r(errno, buffer, 256) };
}


std::string cu_ambiguous_msg(const std::string& name, const compilation_unit& first, const compilation_unit& second)
{
    std::string retval;
    retval.reserve(name.size() + first.name().size() + second.name().size() + 18 + 33 + 8 + 2);
    return retval.append("Compilation unit ")
        .append(name)
        .append(" ambiguous; found two matches: '")
        .append(first.name())
        .append("' and '")
        .append(second.name())
        .append("'");
}

std::string cu_not_found_msg(const std::string& name)
{
    std::string retval;
    retval.reserve(name.size() + 18 + 11);
    return retval.append("Compilation unit ")
        .append(name)
        .append(" not found");
}


// end helper functions


// begin dbg_error

dbg_error::dbg_error(dbg_error_code c, const char* msg) :
    code(c), message(msg)
{}

dbg_error::dbg_error(dbg_error_code c, const std::string& msg) :
    code(c), message(msg)
{}

dbg_error::dbg_error(dbg_error_code c, std::string&& msg) :
    code(c), message(std::move(msg))
{}


// begin compilation_unit


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
    _units()
{
    FILE* img = fopen(filename, "r");
    if (img == NULL)
    {
        err = { dbg_error_code::SYSTEM_ERROR, get_system_error() };
        return;
    }
    dbg_error gli_error = get_line_info(fileno(img));
    if (gli_error)
        err = std::move(gli_error);
    if (fclose(img) != 0)
        err = { dbg_error_code::SYSTEM_ERROR, get_system_error() };
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
        if ((rv = dwarf_next_cu_header(dw_dbg, NULL, NULL, NULL, NULL, &next_cu_size, &dw_err)) != DW_DLV_OK)
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


// operator overloads


std::ostream& tep::operator<<(std::ostream& os, const dbg_error& de)
{
    os << de.message << " (error code "
        << static_cast<std::underlying_type_t<dbg_error_code>>(de.code) << ")";
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
    return os;
}


bool tep::operator==(const compilation_unit& lhs, const compilation_unit& rhs)
{
    return lhs.name() == rhs.name();
}
