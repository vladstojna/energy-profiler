// flags.cpp

#include "flags.hpp"

#include <iostream>

tep::flags::flags(bool pie, bool idle) :
    _pie(pie),
    _obtain_idle(idle)
{}

bool tep::flags::pie() const
{
    return _pie;
}

bool tep::flags::obtain_idle_readings() const
{
    return _obtain_idle;
}


std::ostream& tep::operator<<(std::ostream& os, const flags& f)
{
    static constexpr const char* yes = "yes";
    static constexpr const char* no = "no";

    os << "is the target a PIE? " << (f.pie() ? yes : no);
    os << "\ncollect idle readings? " << (f.obtain_idle_readings() ? yes : no);
    return os;
}
