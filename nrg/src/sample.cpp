// sample.cpp

#include <nrg/error.hpp>
#include <nrg/reader.hpp>
#include <nrg/sample.hpp>

#include <cassert>

using namespace nrgprf;

template<typename T, size_t sz>
static void operator-=(std::array<T, sz>& lhs, const std::array<T, sz>& rhs)
{
    for (typename std::array<T, sz>::size_type ix = 0; ix < lhs.size(); ix++)
    {
        if (lhs[ix] > rhs[ix])
            lhs[ix] -= rhs[ix];
        else
            lhs[ix] = 0;
    }
}

template<typename T, size_t sz>
static void operator+=(std::array<T, sz>& lhs, const std::array<T, sz>& rhs)
{
    for (typename std::array<T, sz>::size_type ix = 0; ix < lhs.size(); ix++)
        lhs[ix] += rhs[ix];
}

#if defined(NRG_X86_64)

sample::sample() :
    cpu{},
    gpu{}
{}

#elif defined(NRG_PPC64)

sample::sample() :
    timestamps{},
    cpu{},
    gpu{}
{}

#endif // defined(NRG_X86_64)

sample::sample(const reader& reader, error& e) :
    sample{}
{
    e = reader.read(*this);
}

#if defined(NRG_X86_64)

bool sample::operator==(const sample& rhs) const
{
    return cpu == rhs.cpu && gpu == rhs.gpu;
}

#elif defined(NRG_PPC64)

bool sample::operator==(const sample& rhs) const
{
    return timestamps == rhs.timestamps && cpu == rhs.cpu && gpu == rhs.gpu;
}

#endif // defined(NRG_X86_64)

bool sample::operator!=(const sample& rhs) const
{
    return !(*this == rhs);
}

sample sample::operator+(const sample& rhs) const
{
    sample retval(*this);
    return retval += rhs;
}

sample sample::operator-(const sample& rhs) const
{
    sample retval(*this);
    return retval -= rhs;
}

sample sample::operator/(sample::value_type rhs) const
{
    sample retval(*this);
    return retval /= rhs;
}

sample sample::operator*(sample::value_type rhs) const
{
    sample retval(*this);
    return retval *= rhs;
}

sample::operator bool() const
{
    sample empty{};
    return *this != empty;
}

#if defined(NRG_X86_64)

sample& sample::operator-=(const sample& rhs)
{
    cpu -= rhs.cpu;
    gpu -= rhs.gpu;
    return *this;
}

sample& sample::operator+=(const sample& rhs)
{
    cpu += rhs.cpu;
    gpu += rhs.gpu;
    return *this;
}

sample& sample::operator/=(sample::value_type rhs)
{
    assert(rhs);
    for (auto& val : cpu)
        val /= rhs;
    for (auto& val : gpu)
        val /= rhs;
    return *this;
}

sample& sample::operator*=(sample::value_type rhs)
{
    for (auto& val : cpu)
        val *= rhs;
    for (auto& val : gpu)
        val *= rhs;
    return *this;
}

#elif defined(NRG_PPC64)

sample& sample::operator-=(const sample& rhs)
{
    timestamps -= rhs.timestamps;
    cpu -= rhs.cpu;
    gpu -= rhs.gpu;
    return *this;
}

sample& sample::operator+=(const sample& rhs)
{
    timestamps -= rhs.timestamps;
    cpu += rhs.cpu;
    gpu += rhs.gpu;
    return *this;
}

sample& sample::operator/=(sample::value_type rhs)
{
    assert(rhs);
    for (auto& val : timestamps)
        val /= rhs;
    for (auto& val : cpu)
        val /= rhs;
    for (auto& val : gpu)
        val /= rhs;
    return *this;
}

sample& sample::operator*=(sample::value_type rhs)
{
    for (auto& val : timestamps)
        val /= rhs;
    for (auto& val : cpu)
        val *= rhs;
    for (auto& val : gpu)
        val *= rhs;
    return *this;
}

#endif // defined(NRG_X86_64)

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
