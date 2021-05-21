// trap.hpp

#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <set>
#include <filesystem>
#include <unordered_map>

namespace nrgprf
{
    class reader_rapl;
    class reader_gpu;
};

namespace tep
{

    // sampler creator hierarchy

    class async_sampler;

    class sampler_creator
    {
    public:
        virtual ~sampler_creator() = default;
        virtual std::unique_ptr<async_sampler> create() const = 0;
    };

    template<typename R>
    class unbounded_creator : public sampler_creator
    {
    private:
        const R* _reader;
        std::chrono::milliseconds _period;
        size_t _initsz;

    public:
        unbounded_creator(const R* reader, const std::chrono::milliseconds& period, size_t initial_size);
        std::unique_ptr<async_sampler> create() const override;
    };

    template<typename R>
    class bounded_creator : public sampler_creator
    {
    private:
        const R* _reader;
        std::chrono::milliseconds _period;

    public:
        bounded_creator(const R* reader, const std::chrono::milliseconds& period);
        std::unique_ptr<async_sampler> create() const override;
    };

    // position related hierarchy

    class position_interface
    {
    public:
        virtual ~position_interface() = default;

        friend std::ostream& operator<<(std::ostream&, const position_interface&);

    private:
        virtual std::ostream& print(std::ostream& os) const = 0;
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
        std::ostream& print(std::ostream& os) const override;
    };

    class position_func : public position_single
    {
    private:
        std::string _name;
        position_line _pos;

    public:
        position_func(const std::string& name, const position_line& pl);
        position_func(const std::string& name, position_line&& pl);
        position_func(std::string&& name, const position_line& pl);
        position_func(std::string&& name, position_line&& pl);

        const std::string& name() const;
        const position_line& line() const;

    private:
        std::ostream& print(std::ostream& os) const override;
    };

    class position_func_off : public position_single
    {
    private:
        position_func _func;
        uintptr_t _offset;

    public:
        position_func_off(const position_func& func, uintptr_t offset);
        position_func_off(position_func&& func, uintptr_t offset);

        const position_func& func() const;
        uintptr_t offset() const;

    private:
        std::ostream& print(std::ostream& os) const override;
    };

    class position_addr : public position_single
    {
    private:
        uintptr_t _addr;

    public:
        position_addr(uintptr_t addr);

        uintptr_t addr() const;

    private:
        std::ostream& print(std::ostream& os) const override;
    };

    template<typename Pos>
    class position_interval : public position_interface
    {
    public:
        using pos_type = Pos;

    private:
        std::array<pos_type, 2> _interval;

    public:
        position_interval(pos_type&& p1, pos_type&& p2);

        const pos_type& start() const;
        const pos_type& end() const;

    private:
        std::ostream& print(std::ostream& os) const override;
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

    class trap
    {
    private:
        long _origword;
        std::unique_ptr<const position_single> _at;

    public:
        trap(long origword, std::unique_ptr<const position_single>&& at);

        long origword() const;
        const position_single& at() const&;
        std::unique_ptr<const position_single> at()&&;

        friend std::ostream& operator<<(std::ostream&, const trap&);

    protected:
        virtual std::ostream& print(std::ostream& os) const;
    };

    class start_trap : public trap
    {
    private:
        std::unique_ptr<const sampler_creator> _creator;

    public:
        start_trap(long origword, std::unique_ptr<const position_single>&& at,
            std::unique_ptr<sampler_creator>&& creator);

        std::unique_ptr<async_sampler> create_sampler() const;
    };

    class end_trap : public trap
    {
    private:
        start_addr _start;

    public:
        end_trap(long origword, std::unique_ptr<const position_single>&& at, start_addr);

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

        // finds the end_trap associated with end_addr which is
        // the end address of section with start at start_addr
        // returns nullptr if not found
        // (i.e., does not exist or, if exists, is not associated with start_addr)
        const end_trap* find(end_addr, start_addr) const;
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

    // types

    using unbounded_rapl_smpcrt = unbounded_creator<nrgprf::reader_rapl>;
    using unbounded_gpu_smpcrt = unbounded_creator<nrgprf::reader_gpu>;
    using bounded_rapl_smpcrt = bounded_creator<nrgprf::reader_rapl>;
    using bounded_gpu_smpcrt = bounded_creator<nrgprf::reader_gpu>;

    using pos_interval_addr = position_interval<position_addr>;
    using pos_interval_line = position_interval<position_line>;

}
