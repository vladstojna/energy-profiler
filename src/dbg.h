// dbg.h
#pragma once

#include <cstdint>
#include <string>
#include <iosfwd>
#include <vector>
#include <map>

namespace tep
{

typedef uint64_t line_no;
typedef uintptr_t line_addr;

class compilation_unit
{
private:
    const std::string _name;
    std::map<uint64_t, std::vector<uintptr_t>> _lines;

public:
    compilation_unit(const char* name);
    compilation_unit(const std::string& name);
    compilation_unit(std::string&& name);
    compilation_unit(compilation_unit&& other);

    // disable copying
    compilation_unit(const compilation_unit& other) = delete;
    compilation_unit& operator=(const compilation_unit& other) = delete;

    const std::string& name()  const { return _name; }
    const decltype(_lines)& lines() const { return _lines; }

    void add_address(uint64_t lineno, uintptr_t lineaddr);
    uintptr_t line_first_addr(uint64_t lineno) const;
    const std::vector<uintptr_t>& line_addrs(uint64_t lineno) const;

    friend std::ostream& operator<<(std::ostream& os, const compilation_unit& cu);
};

class dbg_line_info
{
private:
    std::vector<compilation_unit> _units;

public:
    dbg_line_info(const char* filename);
    dbg_line_info(dbg_line_info&& other);

    // disable copying
    dbg_line_info(const dbg_line_info& other) = delete;
    dbg_line_info& operator=(const dbg_line_info& other) = delete;

    bool has_dbg_symbols() const { return !_units.empty(); }

    const std::vector<compilation_unit>& cus() const { return _units; }
    const compilation_unit& first_cu()         const { return _units.front(); }

    const compilation_unit& cu_by_name(const std::string& name = "") const;

    friend std::ostream& operator<<(std::ostream& os, const dbg_line_info& cu);

private:
    compilation_unit& last_cu() { return _units.back(); }
    void get_line_info(const char* filename);
};

std::ostream& operator<<(std::ostream& os, const compilation_unit& cu);
std::ostream& operator<<(std::ostream& os, const dbg_line_info& cu);

}
