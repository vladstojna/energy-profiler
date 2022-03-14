// sample.cpp

#include <nrg/error.hpp>
#include <nrg/reader.hpp>
#include <nrg/sample.hpp>

#include <cstring>

using namespace nrgprf;

sample::sample() :
    data{}
{}

bool sample::operator==(const sample& rhs) const
{
    return !std::memcmp(this, &rhs, sizeof(rhs));
}

bool sample::operator!=(const sample& rhs) const
{
    return !(*this == rhs);
}

sample::operator bool() const
{
    sample empty{};
    return *this != empty;
}
