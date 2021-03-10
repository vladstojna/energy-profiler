// event_cpu.cpp

#include "event_cpu.hpp"

using namespace nrgprf;

// start helper functions

result<long long> get_value(const sample& s, rapl_domain effd, rapl_domain d, uint8_t idx)
{
    if ((d & effd) == rapl_domain::NONE)
        return error(error_code::NO_EVENT, "no such event");
    return s.get(idx);
}

// end helper functions

event_cpu::event_cpu(rapl_domain effd, uint8_t pkg, uint8_t pp0, uint8_t pp1, uint8_t dram, double mult) :
    _eff_dmask(effd),
    _pkg(pkg),
    _pp0(pp0),
    _pp1(pp1),
    _dram(dram),
    _mult(mult)
{
}

event_cpu::event_cpu() :
    event_cpu(rapl_domain::NONE, 0, 0, 0, 0, 1)
{
}

result<long long> event_cpu::get_pkg(const sample& s) const
{
    return get_value(s, _eff_dmask, rapl_domain::PKG, _pkg);
}

result<long long> event_cpu::get_pp0(const sample& s) const
{
    return get_value(s, _eff_dmask, rapl_domain::PP0, _pp0);
}

result<long long> event_cpu::get_pp1(const sample& s) const
{
    return get_value(s, _eff_dmask, rapl_domain::PP1, _pp1);
}

result<long long> event_cpu::get_dram(const sample& s) const
{
    return get_value(s, _eff_dmask, rapl_domain::DRAM, _dram);
}

std::ostream& nrgprf::operator<<(std::ostream& os, const event_cpu& e)
{
    os << e._eff_dmask
        << ", pkg = " << static_cast<unsigned>(e._pkg)
        << ", pp0 = " << static_cast<unsigned>(e._pp0)
        << ", pp1 = " << static_cast<unsigned>(e._pp1)
        << ", dram = " << static_cast<unsigned>(e._dram);
    return os;
}
