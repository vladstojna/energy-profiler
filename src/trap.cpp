// trap.cpp

#include <cassert>
#include <iomanip>
#include <iostream>

#include "trap.hpp"
#include "sampler.hpp"

using namespace tep;


static std::ostream& print_uintptr(std::ostream& os, uintptr_t ptr)
{
    std::ios::fmtflags flags(os.flags());
    os << "0x" << std::hex << ptr;
    os.flags(flags);
    return os;
}


std::size_t start_addr::hash::operator()(start_addr addr) const
{
    return std::hash<uintptr_t>{}(addr.val());
}

std::size_t end_addr::hash::operator()(end_addr addr) const
{
    return std::hash<uintptr_t>{}(addr.val());
}

end_addr::end_addr(uintptr_t addr) :
    _addr(addr)
{}

start_addr::start_addr(uintptr_t addr) :
    _addr(addr)
{}

uintptr_t end_addr::val() const
{
    return _addr;
}

uintptr_t start_addr::val() const
{
    return _addr;
}

std::size_t addr_bounds_hash::operator()(const addr_bounds& bounds) const
{
    return (start_addr::hash{}(bounds.first) << 32) +
        (end_addr::hash{}(bounds.second) & std::numeric_limits<uint32_t>::max());
}

std::ostream& tep::operator<<(std::ostream& os, start_addr addr)
{
    return print_uintptr(os, addr.val());
}

std::ostream& tep::operator<<(std::ostream& os, end_addr addr)
{
    return print_uintptr(os, addr.val());
}

bool tep::operator==(start_addr lhs, start_addr rhs)
{
    return lhs.val() == rhs.val();
}

bool tep::operator==(end_addr lhs, end_addr rhs)
{
    return lhs.val() == rhs.val();
}

bool tep::operator!=(end_addr lhs, end_addr rhs)
{
    return !(lhs == rhs);
}

bool tep::operator!=(start_addr lhs, start_addr rhs)
{
    return !(lhs == rhs);
}

namespace tep::pos
{

    std::filesystem::path line::filename() const
    {
        return path.filename();
    }

    std::ostream& operator<<(std::ostream& os, const none&)
    {
        return os << "<none>";
    }

    std::ostream& operator<<(std::ostream& os, const line& l)
    {
        return os << l.filename().string() << ":" << l.line;
    }

    std::ostream& operator<<(std::ostream& os, const function& f)
    {
        return os << f.name;
    }

    std::ostream& operator<<(std::ostream& os, const function_full& ff)
    {
        return os << ff.at << ":" << ff.name;
    }

    std::ostream& operator<<(std::ostream& os, const address& a)
    {
        std::ios::fmtflags flags(os.flags());
        os << "0x" << std::hex << a.at;
        os.flags(flags);
        return os;
    }

    std::ostream& operator<<(std::ostream& os, const simple_pos& sp)
    {
        std::visit([&os](auto&& p)
            {
                os << p;
            }, sp);
        return os;
    }

    std::ostream& operator<<(std::ostream& os, const interval& i)
    {
        os << i.start << " - " << i.end;
        return os;
    }

    std::ostream& operator<<(std::ostream& os, const offset& o)
    {
        os << o.start << "+" << address{ o.off };
        return os;
    }

    std::ostream& operator<<(std::ostream& os, const single_pos& sp)
    {
        std::visit([&os](auto&& p)
            {
                os << p;
            }, sp);
        return os;
    }

    std::ostream& operator<<(std::ostream& os, const any& a)
    {
        std::visit([&os](auto&& p)
            {
                os << p;
            }, a);
        return os;
    }
}

std::ostream& tep::operator<<(std::ostream& os, const trap& t)
{
    return t.print(os);
}

trap::trap(long origword, const pos::single_pos& at) :
    _origword(origword),
    _at(at)
{}

trap::trap(long origword, pos::single_pos&& at) :
    _origword(origword),
    _at(std::move(at))
{}

long trap::origword() const
{
    return _origword;
}

pos::single_pos& trap::at()
{
    return _at;
}

const pos::single_pos& trap::at() const
{
    return _at;
}

std::ostream& trap::print(std::ostream& os) const
{
    os << at() << " [";
    std::ios::fmtflags flags(os.flags());
    os << std::hex << std::setfill('0') << std::setw(16) << origword();
    os.flags(flags);
    os << "]";
    return os;
}

bool start_trap::allow_concurrency() const
{
    return _allow_concurrency;
}

std::unique_ptr<async_sampler> start_trap::create_sampler() const
{
    return _creator();
}

end_trap::end_trap(long origword, pos::single_pos&& at, start_addr addr) :
    trap(origword, std::move(at)),
    _start(addr)
{}

end_trap::end_trap(long origword, const pos::single_pos& at, start_addr addr) :
    trap(origword, at),
    _start(addr)
{}

start_addr end_trap::associated_with() const
{
    return _start;
}

std::ostream& end_trap::print(std::ostream& os) const
{
    trap::print(os);
    os << " <-> " << associated_with();
    return os;
}

std::pair<const start_trap*, bool> registered_traps::insert(start_addr a, start_trap&& st)
{
    auto [it, inserted] = _start_traps.insert({ a, std::move(st) });
    return { &it->second, inserted };
}

std::pair<const end_trap*, bool> registered_traps::insert(end_addr a, end_trap&& et)
{
    auto [it, inserted] = _end_traps.insert({ a, std::move(et) });
    return { &it->second, inserted };
}

const start_trap* registered_traps::find(start_addr addr) const
{
    return find_impl(*this, addr);
}

const end_trap* registered_traps::find(end_addr ea, start_addr sa) const
{
    return find_impl(*this, ea, sa);
}

start_trap* registered_traps::find(start_addr addr)
{
    return find_impl(*this, addr);
}

end_trap* registered_traps::find(end_addr ea, start_addr sa)
{
    return find_impl(*this, ea, sa);
}

template<typename T>
auto registered_traps::find_impl(T& instance, start_addr addr)
-> decltype(instance.find(addr))
{
    auto it = instance._start_traps.find(addr);
    if (it == instance._start_traps.end())
        return nullptr;
    return &it->second;
}

template<typename T>
auto registered_traps::find_impl(T& instance, end_addr eaddr, start_addr saddr)
-> decltype(instance.find(eaddr, saddr))
{
    auto it = instance._end_traps.find(eaddr);
    if (it == instance._end_traps.end())
        return nullptr;
    if (it->second.associated_with() != saddr)
        return nullptr;
    return &it->second;
}
