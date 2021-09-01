// dbg.hpp

#pragma once

#include <iosfwd>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <util/expectedfwd.hpp>

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
        FORMAT_ERROR,
        FUNCTION_NOT_FOUND,
        FUNCTION_AMBIGUOUS,
        UNKNOWN_TARGET_TYPE
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
    using dbg_expected = nonstd::expected<R, dbg_error>;

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

        bool contains(const std::string& cu) const;
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
        std::string _suffix;
        position _pos;
        function_bounds _bounds;

    public:
        template<typename N, typename S, typename P, typename B>
        function(N&& name, S&& suffix, P&& pos, B&& bounds) :
            _name(std::forward<N>(name)),
            _suffix(std::forward<S>(suffix)),
            _pos(std::forward<P>(pos)),
            _bounds(std::forward<B>(bounds))
        {}

        std::string name() const;

        const std::string& prototype() const;
        const std::string& suffix() const;
        const position& pos() const;
        const function_bounds& bounds() const;

        bool matches(const std::string& name, const std::string& cu = "") const;
        bool equals(const std::string& name, const std::string& cu = "") const;
    };

    class unit_lines
    {
    private:
        std::string _name;
        std::map<uint32_t, std::set<uintptr_t>> _lines;

    public:
        unit_lines(const char* name);
        unit_lines(const std::string& name);
        unit_lines(std::string&& name);

        void add_address(uint32_t lineno, uintptr_t lineaddr);

        const std::string& name()  const;

        // returns a pair whose first element is the effective line found,
        // i.e. first line greater than or equal to lineno,
        // and whose second element is the lowest address of said effective line
        dbg_expected<std::pair<uint32_t, uintptr_t>>
            lowest_addr(uint32_t lineno) const;

        // returns a pair whose first element is the effective line found,
        // i.e. first line greater than or equal to lineno,
        // and whose second element is the highest address of said effective line
        dbg_expected<std::pair<uint32_t, uintptr_t>>
            highest_addr(uint32_t lineno) const;

        friend std::ostream& operator<<(std::ostream& os, const unit_lines& cu);

    private:
        using addr_getter =
            dbg_expected<uintptr_t>(unit_lines::*)(const decltype(_lines)::mapped_type&) const;

        dbg_expected<std::pair<uint32_t, uintptr_t>>
            get_addr_impl(uint32_t lineno, addr_getter) const;

        dbg_expected<uintptr_t> get_lowest_addr(const decltype(_lines)::mapped_type&) const;
        dbg_expected<uintptr_t> get_highest_addr(const decltype(_lines)::mapped_type&) const;
    };

    class header_info
    {
    public:
        enum class type
        {
            dyn,
            exec,
            unknown
        };

    private:
        type _exectype;

    public:
        header_info(const char* target, dbg_error& err);

        type exec_type() const;

    private:
        dbg_error get_exec_type(const char* target);
    };

    class dbg_info
    {
    public:
        static dbg_expected<dbg_info> create(const char* filename);
        static dbg_expected<dbg_info> create(const std::string& filename);

    private:
        header_info _hi;
        std::vector<unit_lines> _lines;
        std::vector<function> _funcs;

        dbg_info(const char* filename, dbg_error& err);

    public:
        bool has_line_info() const;
        const header_info& header() const;

        dbg_expected<const unit_lines*> find_lines(const std::string& name) const;
        dbg_expected<const unit_lines*> find_lines(const char* name) const;
        dbg_expected<unit_lines*> find_lines(const std::string& name);
        dbg_expected<unit_lines*> find_lines(const char* name);

        dbg_expected<const function*> find_function(const std::string& name,
            const std::string& cu) const;

        friend std::ostream& operator<<(std::ostream& os, const dbg_info& cu);

    private:
        template<typename T>
        static auto find_lines_impl(T& instance, const char* name)
            -> decltype(instance.find_lines(name));

        dbg_error get_line_info(int fd);
        dbg_error get_functions(const char* filename);
    };

    // operator overloads

    std::ostream& operator<<(std::ostream& os, const dbg_error& de);
    std::ostream& operator<<(std::ostream& os, const position& p);
    std::ostream& operator<<(std::ostream& os, const function_bounds& fb);
    std::ostream& operator<<(std::ostream& os, const function& f);
    std::ostream& operator<<(std::ostream& os, const unit_lines& cu);
    std::ostream& operator<<(std::ostream& os, const header_info& hi);
    std::ostream& operator<<(std::ostream& os, const dbg_info& cu);

    bool operator==(const unit_lines& lhs, const unit_lines& rhs);

}
