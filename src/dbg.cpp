// dbg.cpp
#include <libdwarf/libdwarf.h>

#include "dbg.h"

#include <stdexcept>
#include <iostream>
#include <iomanip>

#define str(x) #x
#define stringify(x) str(x)
#define fl __FILE__ "@" stringify(__LINE__) ": "

// begin helper functions

static void handle_dwarf_error(int result_value, const char* msg, Dwarf_Error error)
{
    if (result_value == DW_DLV_NO_ENTRY)
    {
        fprintf(stderr, "%s libdwarf: no debug information present\n", msg);
        throw std::runtime_error("no debug info");
    }
    if (result_value == DW_DLV_ERROR)
    {
        fprintf(stderr, "%s libdwarf error: %s\n", msg, dwarf_errmsg(error));
        throw std::runtime_error("dwarf error");
    }
    return throw std::runtime_error("generic error");
}

// end helper functions

tep::compilation_unit::compilation_unit(const char* name) :
    _name(name),
    _lines()
{
}

tep::compilation_unit::compilation_unit(const std::string& name) :
    _name(name),
    _lines()
{
}

tep::compilation_unit::compilation_unit(std::string&& name) :
    _name(std::move(name)),
    _lines()
{
}

tep::compilation_unit::compilation_unit(compilation_unit&& other) :
    _name(std::move(other._name)),
    _lines(std::move(other._lines))
{
}

void tep::compilation_unit::add_address(line_no lineno, tep::line_addr lineaddr)
{
    if (_lines.find(lineno) == _lines.end())
    {
        _lines.try_emplace(lineno);
    }
    _lines.at(lineno).emplace_back(lineaddr);
}

tep::line_addr tep::compilation_unit::line_first_addr(line_no lineno) const
{
    for (const auto& [no, addrs] : _lines)
    {
        if (no >= lineno)
        {
            // always at least one address present
            return addrs.front();
        }
    }
    throw std::invalid_argument("invalid line");
}

const std::vector<tep::line_addr>& tep::compilation_unit::line_addrs(line_no lineno) const
{
    for (const auto& [no, addrs] : _lines)
    {
        if (no >= lineno)
        {
            return addrs;
        }
    }
    throw std::invalid_argument("invalid line");
}

tep::dbg_line_info::dbg_line_info(const char* filename) :
    _units()
{
    get_line_info(filename);
}

tep::dbg_line_info::dbg_line_info(dbg_line_info&& other) :
    _units(std::move(other._units))
{
}

const tep::compilation_unit& tep::dbg_line_info::cu_by_name(const std::string& name) const
{
    if (name.empty())
    {
        return _units.front(); // first CU
    }
    for (const auto& cu : _units)
    {
        if (cu.name() == name)
            return cu;
    }
    throw std::invalid_argument("compilation unit not found");
}

void tep::dbg_line_info::get_line_info(const char* filename)
{
    FILE* img = fopen(filename, "r");
    if (img == NULL)
    {
        perror(fl "fopen");
        throw std::runtime_error("generic error");
    }

    int rv;

    Dwarf_Debug dw_dbg;
    Dwarf_Error dw_err;
    if ((rv = dwarf_init(fileno(img), DW_DLC_READ, NULL, NULL, &dw_dbg, &dw_err)) != DW_DLV_OK)
    {
        handle_dwarf_error(rv, fl, dw_err);
    }

    // iterate all compilation units
    while (true)
    {
        Dwarf_Unsigned next_cu_size;
        if ((rv = dwarf_next_cu_header(dw_dbg, NULL, NULL, NULL, NULL, &next_cu_size, &dw_err)) != DW_DLV_OK)
        {
            // if no more compilation units left
            if (rv == DW_DLV_NO_ENTRY)
                break;
            handle_dwarf_error(rv, fl, dw_err);
        }

        Dwarf_Die dw_die;
        if ((rv = dwarf_siblingof(dw_dbg, NULL, &dw_die, &dw_err)) != DW_DLV_OK)
        {
            handle_dwarf_error(rv, fl, dw_err);
        }

        char* comp_unit;
        if ((rv = dwarf_diename(dw_die, &comp_unit, &dw_err)) != DW_DLV_OK)
        {
            handle_dwarf_error(rv, fl, dw_err);
        }

        _units.emplace_back(comp_unit);

        Dwarf_Line* linebuf;
        Dwarf_Signed linecount;
        if ((rv = dwarf_srclines(dw_die, &linebuf, &linecount, &dw_err)) != DW_DLV_OK)
        {
            handle_dwarf_error(rv, fl, dw_err);
        }

        for (Dwarf_Signed ix = 0; ix < linecount; ix++)
        {
            Dwarf_Bool result;
            if ((rv = dwarf_linebeginstatement(linebuf[ix], &result, &dw_err)) != DW_DLV_OK)
            {
                handle_dwarf_error(rv, fl, dw_err);
            }
            // proceed only if line represents beginning of statement
            if (!result)
                continue;

            Dwarf_Unsigned lineno;
            if ((rv = dwarf_lineno(linebuf[ix], &lineno, &dw_err)) != DW_DLV_OK)
            {
                handle_dwarf_error(rv, fl, dw_err);
            }

            Dwarf_Addr lineaddr;
            if ((rv = dwarf_lineaddr(linebuf[ix], &lineaddr, &dw_err)) != DW_DLV_OK)
            {
                handle_dwarf_error(rv, fl, dw_err);
            }

            last_cu().add_address(lineno, lineaddr);
        }

        for (Dwarf_Signed ix = 0; ix < linecount; ix++)
        {
            dwarf_dealloc(dw_dbg, linebuf[ix], DW_DLA_LINE);
        }
        dwarf_dealloc(dw_dbg, linebuf, DW_DLA_LIST);
        dwarf_dealloc(dw_dbg, comp_unit, DW_DLA_STRING);
    }

    if (dwarf_finish(dw_dbg, &dw_err) != DW_DLV_OK)
    {
        handle_dwarf_error(rv, fl, dw_err);
    }

    if (fclose(img) != 0)
    {
        perror(fl "fclose");
        throw std::runtime_error("generic error");
    }
}

// operator overloads

std::ostream& tep::operator<<(std::ostream& os, const compilation_unit& cu)
{
    for (const auto& [no, addrs] : cu._lines)
    {
        std::ios::fmtflags os_flags(os.flags());
        os << cu._name << ":" << no << " @" << std::hex;
        for (const auto& addr : addrs)
        {
            os << " 0x" << addr;
        }
        os.flags(os_flags);
        os << "\n";
    }
    return os;
}

std::ostream& tep::operator<<(std::ostream& os, const dbg_line_info& dbg_info)
{
    for (const auto& cu : dbg_info._units)
    {
        os << cu;
    }
    return os;
}
