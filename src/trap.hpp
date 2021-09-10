// trap.hpp

#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <unordered_map>
#include <variant>

namespace tep
{

    class sampler;

    using sampler_creator = std::function<std::unique_ptr<sampler>()>;

    namespace pos
    {
        struct none
        {};

        struct line
        {
            std::filesystem::path path;
            uint32_t line;
            std::filesystem::path filename() const;
        };

        struct function
        {
            std::string name;
        };

        struct function_full : function
        {
            line at;
        };

        struct address
        {
            uintptr_t at;
        };

        using named_pos = std::variant<line, function, function_full>;

        struct offset
        {
            named_pos start;
            uintptr_t off;
        };

        using single_pos = std::variant<address, named_pos, offset>;

        struct interval
        {
            single_pos start;
            single_pos end;
        };

        using any = std::variant<none, single_pos, interval>;

        std::ostream& operator<<(std::ostream& os, const none&);
        std::ostream& operator<<(std::ostream& os, const line&);
        std::ostream& operator<<(std::ostream& os, const function&);
        std::ostream& operator<<(std::ostream& os, const function_full&);
        std::ostream& operator<<(std::ostream& os, const address&);
        std::ostream& operator<<(std::ostream& os, const named_pos&);
        std::ostream& operator<<(std::ostream& os, const offset&);
        std::ostream& operator<<(std::ostream& os, const single_pos&);
        std::ostream& operator<<(std::ostream& os, const interval&);
        std::ostream& operator<<(std::ostream& os, const any&);
    }

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
        pos::single_pos _at;

    protected:
        ~trap() = default;

    public:
        trap(long origword, const pos::single_pos& at);
        trap(long origword, pos::single_pos&& at);

        trap(trap&& other) = default;
        trap& operator=(trap&& other) = default;

        long origword() const;
        pos::single_pos& at();
        const pos::single_pos& at() const;

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
        start_trap(
            long origword,
            pos::single_pos&& at,
            bool allow_concurrency,
            Creator&& callable)
            :
            trap(origword, std::move(at)),
            _allow_concurrency(allow_concurrency),
            _creator(std::forward<Creator>(callable))
        {}

        template<typename Creator>
        start_trap(
            long origword,
            const pos::single_pos& at,
            bool allow_concurrency,
            Creator&& callable)
            :
            trap(origword, at),
            _allow_concurrency(allow_concurrency),
            _creator(std::forward<Creator>(callable))
        {}

        bool allow_concurrency() const;
        std::unique_ptr<sampler> create_sampler() const;
    };

    class end_trap : public trap
    {
    private:
        start_addr _start;

    public:
        end_trap(long origword, pos::single_pos&&, start_addr);
        end_trap(long origword, const pos::single_pos&, start_addr);

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

    std::ostream& operator<<(std::ostream&, const trap&);

}
