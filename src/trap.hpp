// trap.hpp

#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <unordered_map>

namespace tep
{

    class async_sampler;

    using sampler_creator = std::function<std::unique_ptr<async_sampler>()>;

    // position related hierarchy

    class position_interface
    {
    public:
        virtual ~position_interface() = default;

        friend std::ostream& operator<<(std::ostream&, const position_interface&);

    private:
        virtual void print(std::ostream& os) const = 0;
    };

    class position_single : public position_interface
    {};

    class position_line : public position_single
    {
    private:
        std::filesystem::path _file;
        uint32_t _line;

    public:
        position_line(const std::filesystem::path& f, uint32_t line);
        position_line(std::filesystem::path&& f, uint32_t line);

        std::filesystem::path filename() const;
        const std::filesystem::path& path() const;
        uint32_t lineno() const;

    private:
        void print(std::ostream& os) const override;
    };

    class position_func : public position_single
    {
    private:
        std::string _name;

    public:
        position_func(const std::string& name);
        position_func(std::string&& name);

        const std::string& name() const;

    private:
        void print(std::ostream& os) const override;
    };

    class position_func_full : public position_func
    {
    private:
        position_line _pos;

    public:
        position_func_full(const std::string& name, const position_line& pl);
        position_func_full(const std::string& name, position_line&& pl);
        position_func_full(std::string&& name, const position_line& pl);
        position_func_full(std::string&& name, position_line&& pl);

        const position_line& line() const;

    private:
        void print(std::ostream& os) const override;
    };

    class position_offset : public position_single
    {
    private:
        std::unique_ptr<position_single> _pos;
        uintptr_t _offset;

    public:
        position_offset(std::unique_ptr<position_single>&& pos, uintptr_t offset);

        const position_single& pos() const;
        uintptr_t offset() const;

    private:
        void print(std::ostream& os) const override;
    };

    class position_addr : public position_single
    {
    private:
        uintptr_t _addr;

    public:
        position_addr(uintptr_t addr);

        uintptr_t addr() const;

    private:
        void print(std::ostream& os) const override;
    };

    class position_interval : public position_interface
    {
    private:
        std::array<std::unique_ptr<position_single>, 2> _interval;

    public:
        position_interval(std::unique_ptr<position_single>&& p1, std::unique_ptr<position_single>&& p2);

        const position_single& start() const;
        const position_single& end() const;

    private:
        void print(std::ostream& os) const override;
    };

    // trap related classes

    class start_addr
    {
    private:
        uintptr_t _addr;

    public:
        struct hash
        {
            std::size_t operator()(start_addr addr) const;
        };

        start_addr(uintptr_t addr);

        uintptr_t val() const;
    };

    class end_addr
    {
    private:
        uintptr_t _addr;

    public:
        struct hash
        {
            std::size_t operator()(end_addr addr) const;
        };

        end_addr(uintptr_t addr);

        uintptr_t val() const;
    };

    using addr_bounds = std::pair<start_addr, end_addr>;

    struct addr_bounds_hash
    {
        std::size_t operator()(const addr_bounds& bounds) const;
    };

    class trap
    {
    private:
        long _origword;
        std::unique_ptr<position_single> _at;

    protected:
        ~trap() = default;

    public:
        trap(long origword, std::unique_ptr<position_single>&& at);

        trap(trap&& other) = default;
        trap& operator=(trap&& other) = default;

        long origword() const;
        position_single& at()&;
        const position_single& at() const&;
        std::unique_ptr<position_single>&& at()&&;

        friend std::ostream& operator<<(std::ostream&, const trap&);

    protected:
        virtual std::ostream& print(std::ostream& os) const;
    };

    class start_trap : public trap
    {
    private:
        bool _allow_concurrency;
        sampler_creator _creator;

    public:
        template<typename Creator>
        start_trap(long origword, std::unique_ptr<position_single>&& at,
            bool allow_concurrency, Creator&& callable) :
            trap(origword, std::move(at)),
            _allow_concurrency(allow_concurrency),
            _creator(std::forward<Creator>(callable))
        {}

        bool allow_concurrency() const;
        std::unique_ptr<async_sampler> create_sampler() const;
    };

    class end_trap : public trap
    {
    private:
        start_addr _start;

    public:
        end_trap(long origword, std::unique_ptr<position_single>&& at, start_addr);

        start_addr associated_with() const;

    protected:
        std::ostream& print(std::ostream& os) const override;
    };

    class registered_traps
    {
    private:
        using start_traps = std::unordered_map<start_addr, start_trap, start_addr::hash>;
        using end_traps = std::unordered_map<end_addr, end_trap, end_addr::hash>;
        start_traps _start_traps;
        end_traps _end_traps;

    public:
        std::pair<const start_trap*, bool> insert(start_addr, start_trap&&);
        std::pair<const end_trap*, bool> insert(end_addr, end_trap&&);

        // finds the start_trap associated with start_addr
        // returns nullptr if not found
        const start_trap* find(start_addr) const;
        start_trap* find(start_addr);

        // finds the end_trap associated with end_addr which is
        // the end address of section with start at start_addr
        // returns nullptr if not found
        // (i.e., does not exist or, if exists, is not associated with start_addr)
        const end_trap* find(end_addr, start_addr) const;
        end_trap* find(end_addr, start_addr);

    private:
        template<typename T>
        static auto find_impl(T& instance, start_addr addr)
            -> decltype(instance.find(addr));

        template<typename T>
        static auto find_impl(T& instance, end_addr eaddr, start_addr saddr)
            -> decltype(instance.find(eaddr, saddr));
    };

    // operator overloads

    std::ostream& operator<<(std::ostream&, start_addr);
    std::ostream& operator<<(std::ostream&, end_addr);

    bool operator==(start_addr lhs, start_addr rhs);
    bool operator!=(start_addr lhs, start_addr rhs);
    bool operator==(end_addr lhs, end_addr rhs);
    bool operator!=(end_addr lhs, end_addr rhs);

    std::ostream& operator<<(std::ostream&, const position_interface&);
    std::ostream& operator<<(std::ostream&, const trap&);

}
