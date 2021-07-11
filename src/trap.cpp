// trap.cpp

#include <cassert>
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



position_line::position_line(const std::filesystem::path& f, uint32_t line) :
    _file(f),
    _line(line)
{}

position_line::position_line(std::filesystem::path&& f, uint32_t line) :
    _file(std::move(f)),
    _line(line)
{}

std::filesystem::path position_line::filename() const
{
    return _file.filename();
}

const std::filesystem::path& position_line::path() const
{
    return _file;
}

uint32_t position_line::lineno() const
{
    return _line;
}

void position_line::print(std::ostream& os) const
{
    os << filename().string() << ":" << lineno();
}



position_func::position_func(const std::string& name) :
    _name(name)
{}

position_func::position_func(std::string&& name) :
    _name(std::move(name))
{}

const std::string& position_func::name() const
{
    return _name;
}

void position_func::print(std::ostream& os) const
{
    os << name();
}



position_func_full::position_func_full(const std::string& name, const position_line& pl) :
    position_func(name),
    _pos(pl)
{}

position_func_full::position_func_full(const std::string& name, position_line&& pl) :
    position_func(name),
    _pos(std::move(pl))
{}

position_func_full::position_func_full(std::string&& name, const position_line& pl) :
    position_func(std::move(name)),
    _pos(pl)
{}

position_func_full::position_func_full(std::string&& name, position_line&& pl) :
    position_func(std::move(name)),
    _pos(std::move(pl))
{}

const position_line& position_func_full::line() const
{
    return _pos;
}

void position_func_full::print(std::ostream& os) const
{
    os << line() << ":" << name();
}



position_offset::position_offset(std::unique_ptr<position_single>&& pos, uintptr_t offset) :
    _pos(std::move(pos)),
    _offset(offset)
{}

const position_single& position_offset::pos() const
{
    assert(_pos);
    return *_pos;
}

uintptr_t position_offset::offset() const
{
    return _offset;
}

void position_offset::print(std::ostream& os) const
{
    os << pos() << "+";
    print_uintptr(os, offset());
}



position_addr::position_addr(uintptr_t addr) :
    _addr(addr)
{}

uintptr_t position_addr::addr() const
{
    return _addr;
}

void position_addr::print(std::ostream& os) const
{
    print_uintptr(os, addr());
}



position_interval::position_interval(
    std::unique_ptr<position_single>&& p1,
    std::unique_ptr<position_single>&& p2) :
    _interval{ std::move(p1), std::move(p2) }
{}

const position_single& position_interval::start() const
{
    assert(_interval[0]);
    return *_interval[0];
}

const position_single& position_interval::end() const
{
    assert(_interval[1]);
    return *_interval[1];
}

void position_interval::print(std::ostream& os) const
{
    os << start() << " - " << end();
}


std::ostream& tep::operator<<(std::ostream& os, const position_interface& pi)
{
    pi.print(os);
    return os;
}

std::ostream& tep::operator<<(std::ostream& os, const trap& t)
{
    return t.print(os);
}


trap::trap(long origword, std::unique_ptr<position_single>&& at) :
    _origword(origword),
    _at(std::move(at))
{}

long trap::origword() const
{
    return _origword;
}

position_single& trap::at()&
{
    return *_at;
}

const position_single& trap::at() const&
{
    return *_at;
}

std::unique_ptr<position_single>&& trap::at()&&
{
    return std::move(_at);
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


end_trap::end_trap(long origword, std::unique_ptr<position_single>&& at, start_addr sa) :
    trap(origword, std::move(at)),
    _start(sa)
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
