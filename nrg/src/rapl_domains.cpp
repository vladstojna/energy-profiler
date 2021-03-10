// rapl_domains.cpp

#include "rapl_domains.hpp"

#include <iostream>
#include <iomanip>

std::ostream& nrgprf::operator<<(std::ostream& os, const nrgprf::rapl_domain& rd)
{
    std::ios::fmtflags os_flags(os.flags());
    os.setf(std::ios::hex);
    os << "0x" << static_cast<unsigned>(rd);
    os.flags(os_flags);
    return os;
}
