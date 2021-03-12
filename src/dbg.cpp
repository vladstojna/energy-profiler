// dbg.cpp

#include <libdwarf/libdwarf.h>

#include "dbg.h"

#include <stdexcept>
#include <iostream>
#include <iomanip>
#include <cstring>
#include <cerrno>

using namespace tep;

// begin dbg_error

dbg_error::dbg_error(dbg_error_code c, const char* msg) :
    code(c), message(msg)
{
}

dbg_error::dbg_error(dbg_error_code c, const std::string& msg) :
    code(c), message(msg)
{
}

dbg_error::dbg_error(dbg_error_code c, std::string&& msg) :
    code(c), message(std::move(msg))
{
}

// template specialisations


size_t compilation_unit::hash::operator()(const compilation_unit& cu) const
{
    return std::hash<std::string>()(cu.name());
}

size_t compilation_unit::hash::operator()(const std::string& cu_name) const
{
    return std::hash<std::string>()(cu_name);
}

bool compilation_unit::equal::operator()(const compilation_unit& lhs, const compilation_unit& rhs) const
{
    return lhs.name() == rhs.name();
}

bool compilation_unit::equal::operator()(const compilation_unit& lhs, const std::string& rhs) const
{
    return lhs.name() == rhs;
}

bool compilation_unit::equal::operator()(const std::string& lhs, const compilation_unit& rhs) const
{
    return lhs == rhs.name();
}


// begin compilation_unit


compilation_unit::compilation_unit(const char* name) :
    _name(name),
    _lines()
{
}

compilation_unit::compilation_unit(const std::string& name) :
    _name(name),
    _lines()
{
}

compilation_unit::compilation_unit(std::string&& name) :
    _name(std::move(name)),
    _lines()
{
}

void compilation_unit::add_address(uint32_t lineno, uintptr_t lineaddr)
{
    if (_lines.find(lineno) == _lines.end())
        _lines.try_emplace(lineno);
    _lines.at(lineno).emplace_back(lineaddr);
}

uintptr_t compilation_unit::line_first_addr(uint32_t lineno) const
{
    return line_addrs(lineno).front();
}

const std::vector<uintptr_t>& compilation_unit::line_addrs(uint32_t lineno) const
{
    for (const auto& [no, addrs] : _lines)
    {
        if (no >= lineno)
            return addrs;
    }
    throw std::invalid_argument("invalid line");
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
        err = { dbg_error_code::SYSTEM_ERROR, strerror(errno) };
        return;
    }
    dbg_error gli_error = get_line_info(fileno(img));
    if (gli_error)
        err = std::move(gli_error);
    if (fclose(img) != 0)
        err = { dbg_error_code::SYSTEM_ERROR, strerror(errno) };
}

dbg_expected<const compilation_unit*> dbg_line_info::find_cu(const std::string& name) const
{
    auto it = _units.find(name);
    if (it == _units.end())
        return dbg_error(dbg_error_code::COMPILATION_UNIT_NOT_FOUND,
            "Compilation unit " + name + " not found");
    return &*it;
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
        char* comp_unit;
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
        if ((rv = dwarf_diename(dw_die, &comp_unit, &dw_err)) != DW_DLV_OK)
            return { dbg_error_code::DWARF_ERROR, dwarf_errmsg(dw_err) };
        if ((rv = dwarf_srclines(dw_die, &linebuf, &linecount, &dw_err)) != DW_DLV_OK)
            return { dbg_error_code::DWARF_ERROR, dwarf_errmsg(dw_err) };

        compilation_unit cu(comp_unit);
        for (Dwarf_Signed ix = 0; ix < linecount; ix++)
        {
            Dwarf_Bool result;
            Dwarf_Unsigned lineno;
            Dwarf_Addr lineaddr;
            if ((rv = dwarf_linebeginstatement(linebuf[ix], &result, &dw_err)) != DW_DLV_OK)
                return { dbg_error_code::DWARF_ERROR, dwarf_errmsg(dw_err) };
            // proceed only if line represents beginning of statement
            if (!result)
                continue;
            if ((rv = dwarf_lineno(linebuf[ix], &lineno, &dw_err)) != DW_DLV_OK)
                return { dbg_error_code::DWARF_ERROR, dwarf_errmsg(dw_err) };
            if ((rv = dwarf_lineaddr(linebuf[ix], &lineaddr, &dw_err)) != DW_DLV_OK)
                return { dbg_error_code::DWARF_ERROR, dwarf_errmsg(dw_err) };

            dwarf_dealloc(dw_dbg, linebuf[ix], DW_DLA_LINE);
            cu.add_address(lineno, lineaddr);
        }
        dwarf_dealloc(dw_dbg, linebuf, DW_DLA_LIST);
        dwarf_dealloc(dw_dbg, comp_unit, DW_DLA_STRING);
        _units.insert(std::move(cu));
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
