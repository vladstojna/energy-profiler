// task.cpp

#include <nrg/task.hpp>

using namespace nrgprf;

template<>
template<>
execution& task::add()
{
    return _values.emplace_back(_values.size());
}
