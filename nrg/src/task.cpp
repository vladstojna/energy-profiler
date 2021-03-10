// task.cpp

#include "task.hpp"

using namespace nrgprf;

template<>
template<>
size_t task::add()
{
    size_t sz = _values.size();
    _values.emplace_back(sz);
    return sz;
}
