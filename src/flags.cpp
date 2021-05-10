// flags.cpp

#include "flags.hpp"

#include <iostream>

tep::flags::flags(bool idle) :
    _obtain_idle(idle)
{}

bool tep::flags::obtain_idle_readings() const
{
    return _obtain_idle;
}


std::ostream& tep::operator<<(std::ostream& os, const flags& f)
{
    static constexpr const char* yes = "yes";
    static constexpr const char* no = "no";

    os << "collect idle readings? " << (f.obtain_idle_readings() ? yes : no);
    return os;
}
