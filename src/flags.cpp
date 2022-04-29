// flags.cpp

#include "flags.hpp"

#include <iostream>

std::ostream &tep::operator<<(std::ostream &os, const flags &f) {
  os << "collect idle readings? " << (f.obtain_idle ? "yes" : "no") << ", ";
  os << "CPU sensor location mask: " << f.locations << ", ";
  os << "CPU socket mask: " << f.sockets << ", ";
  os << "GPU device mask: " << f.devices;
  return os;
}
