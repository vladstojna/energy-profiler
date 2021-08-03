// sample.cpp

#include <nrg/error.hpp>
#include <nrg/reader.hpp>
#include <nrg/sample.hpp>

#include <cstring>

using namespace nrgprf;

sample::sample() :
    data{}
{}

sample::sample(const reader& reader, error& e) :
    sample()
{
    e = reader.read(*this);
}

bool sample::operator==(const sample& rhs) const
{
    return std::memcmp(this, &rhs, sizeof(rhs));
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

timed_sample::timed_sample() :
    _timepoint{},
    _sample{}
{}

timed_sample::timed_sample(const reader& reader, error& e) :
    _timepoint(time_point::clock::now()),
    _sample(reader, e)
{}

const timed_sample::time_point& timed_sample::timepoint() const
{
    return _timepoint;
}

bool timed_sample::operator==(const timed_sample& rhs) const
{
    return timepoint() == rhs.timepoint() && sample(*this) == sample(rhs);
}

bool timed_sample::operator!=(const timed_sample& rhs) const
{
    return timepoint() != rhs.timepoint() && sample(*this) == sample(rhs);
}

timed_sample::duration timed_sample::operator-(const timed_sample& rhs) const
{
    return timepoint() - rhs.timepoint();
}

timed_sample::operator const nrgprf::sample& () const
{
    return _sample;
}

timed_sample::operator nrgprf::sample& ()
{
    return _sample;
}
