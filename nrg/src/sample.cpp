// sample.cpp

#include <nrg/sample.hpp>
#include <nrg/reader.hpp>

#include <cassert>

using namespace nrgprf;


sample::sample(const reader& rdr, error& e) :
    _values{}
{
    e = rdr.read(*this);
}

sample::value_type& sample::at_cpu(size_t idx)
{
    assert(idx < max_cpu_events);
    return _values[idx];
}

sample::value_type& sample::at_gpu(size_t idx)
{
    assert(idx < max_gpu_events);
    return _values[max_cpu_events + idx];
}

result<sample::value_type> sample::at_cpu(size_t idx) const
{
    assert(idx < max_cpu_events);
    if (_values[idx] == 0)
        return error(error_code::NO_EVENT);
    return _values[idx];
}

result<sample::value_type> sample::at_gpu(size_t idx) const
{
    assert(idx < max_gpu_events);
    if (_values[max_cpu_events + idx] == 0)
        return error(error_code::NO_EVENT);
    return _values[max_cpu_events + idx];
}


bool sample::operator==(const sample& rhs) const
{
    return _values == rhs._values;
}

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

sample& sample::operator-=(const sample& rhs)
{
    for (decltype(_values)::size_type ix = 0; ix < _values.size(); ix++)
    {
        if (_values[ix] > rhs._values[ix])
            _values[ix] -= rhs._values[ix];
        else
            _values[ix] = 0;
    }
    return *this;
}

sample& sample::operator/=(sample::value_type rhs)
{
    assert(rhs);
    for (auto& val : _values)
        val /= rhs;
    return *this;
}

sample& sample::operator*=(sample::value_type rhs)
{
    for (auto& val : _values)
        val *= rhs;
    return *this;
}

sample& sample::operator+=(const sample& rhs)
{
    for (decltype(_values)::size_type ix = 0; ix < _values.size(); ix++)
        _values[ix] += rhs._values[ix];
    return *this;
}


timed_sample::timed_sample(const reader& reader, error& e) :
    _timepoint(time_point::clock::now()),
    _sample(reader, e)
{}

sample& timed_sample::smp()
{
    return _sample;
}

const sample& timed_sample::smp() const
{
    return _sample;
}

const timed_sample::time_point& timed_sample::timepoint() const
{
    return _timepoint;
}

bool timed_sample::operator==(const timed_sample& rhs)
{
    return timepoint() == rhs.timepoint() && smp() == rhs.smp();
}

bool timed_sample::operator!=(const timed_sample& rhs)
{
    return !(*this == rhs);
}

timed_sample::duration timed_sample::operator-(const timed_sample& rhs)
{
    return timepoint() - rhs.timepoint();
}
