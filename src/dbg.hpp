// dbg.hpp

#pragma once

#include <string>
#include <iosfwd>
#include <vector>
#include <map>

namespace cmmn
{
    template<typename R, typename E>
    class expected;
}

namespace tep
{

    // error handling

    enum class dbg_error_code
    {
        SUCCESS = 0,
        SYSTEM_ERROR,
        DEBUG_SYMBOLS_NOT_FOUND,
        COMPILATION_UNIT_NOT_FOUND,
        COMPILATION_UNIT_AMBIGUOUS,
        INVALID_LINE,
        DWARF_ERROR,
        PIPE_ERROR,
        FORMAT_ERROR
    };

    struct dbg_error
    {
        static dbg_error success();

        dbg_error_code code;
        std::string message;

        dbg_error(dbg_error_code c, const char* msg);
        dbg_error(dbg_error_code c, const std::string& msg);
        dbg_error(dbg_error_code c, std::string&& msg);

        explicit operator bool() const;
    };

    // types

    template<typename R>
    using dbg_expected = cmmn::expected<R, dbg_error>;

    // classes

    class position
    {
    private:
        std::string _cu;
        uint32_t _line;

    public:
        position(const std::string& cu, uint32_t line);
        position(std::string&& cu, uint32_t line);
        position(const char* cu, uint32_t line);

        const std::string& cu() const;
        uint32_t line() const;
    };

    class function_bounds
    {
    private:
        uintptr_t _start;
        std::vector<uintptr_t> _rets;

    public:
        function_bounds(uintptr_t start, const std::vector<uintptr_t>& rets);
        function_bounds(uintptr_t start, std::vector<uintptr_t>&& rets);

        uintptr_t start() const;
        const std::vector<uintptr_t>& returns() const;
    };

    class function
    {
    private:
        std::string _name;
        position _pos;
        function_bounds _bounds;

    public:
        function(const std::string& name, const position& pos, const function_bounds& bounds);
        function(const std::string& name, const position& pos, function_bounds&& bounds);
        function(const std::string& name, position&& pos, const function_bounds& bounds);
        function(const std::string& name, position&& pos, function_bounds&& bounds);
        function(std::string&& name, const position& pos, const function_bounds& bounds);
        function(std::string&& name, const position& pos, function_bounds&& bounds);
        function(std::string&& name, position&& pos, const function_bounds& bounds);
        function(std::string&& name, position&& pos, function_bounds&& bounds);

        const std::string& name() const;
        const position& pos() const;
        const function_bounds& bounds() const;
    };

    class compilation_unit
    {
    private:
        std::string _name;
        std::map<uint32_t, std::vector<uintptr_t>> _lines;

    public:
        compilation_unit(const char* name);
        compilation_unit(const std::string& name);
        compilation_unit(std::string&& name);

        const std::string& name()  const { return _name; }
        void add_address(uint32_t lineno, uintptr_t lineaddr);
        dbg_expected<uintptr_t> line_first_addr(uint32_t lineno) const;
        dbg_expected<uintptr_t> line_addr(uint32_t lineno, size_t order) const;

        friend std::ostream& operator<<(std::ostream& os, const compilation_unit& cu);
    };

    class dbg_line_info
    {
    public:
        static dbg_expected<dbg_line_info> create(const char* filename);

    private:
        std::vector<compilation_unit> _units;
        std::vector<function> _funcs;

        dbg_line_info(const char* filename, dbg_error& err);

    public:
        bool has_dbg_symbols() const;

        dbg_expected<const compilation_unit*> find_cu(const std::string& name) const;
        dbg_expected<const compilation_unit*> find_cu(const char* name) const;
        dbg_expected<compilation_unit*> find_cu(const std::string& name);
        dbg_expected<compilation_unit*> find_cu(const char* name);

        friend std::ostream& operator<<(std::ostream& os, const dbg_line_info& cu);

    private:
        dbg_error get_line_info(int fd);
        dbg_error get_functions(const char* filename);
    };

    // operator overloads

    std::ostream& operator<<(std::ostream& os, const dbg_error& de);
    std::ostream& operator<<(std::ostream& os, const position& p);
    std::ostream& operator<<(std::ostream& os, const function_bounds& fb);
    std::ostream& operator<<(std::ostream& os, const function& f);
    std::ostream& operator<<(std::ostream& os, const compilation_unit& cu);
    std::ostream& operator<<(std::ostream& os, const dbg_line_info& cu);

    bool operator==(const compilation_unit& lhs, const compilation_unit& rhs);

}
