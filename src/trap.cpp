// trap.cpp

#include "trap.hpp"

using namespace tep;


trap_data::trap_data(uintptr_t addr, long ow, const config_data::section& sec) :
    _addr(addr),
    _origw(ow),
    _section(&sec)
{}

uintptr_t trap_data::address() const
{
    return _addr;
}

long trap_data::original_word() const
{
    return _origw;
}

const config_data::section& trap_data::section() const
{
    return *_section;
}


bool tep::operator<(const trap_data& lhs, const trap_data& rhs)
{
    return lhs.address() < rhs.address();
}

bool tep::operator<(uintptr_t lhs, const trap_data& rhs)
{
    return lhs < rhs.address();
}

bool tep::operator<(const trap_data& lhs, uintptr_t rhs)
{
    return lhs.address() < rhs;
}
