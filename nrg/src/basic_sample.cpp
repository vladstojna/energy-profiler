// basic_sample.cpp

#include <nrg/basic_sample.hpp>

using namespace nrgprf;

basic_sample::basic_sample(const timepoint_t& tp) :
    _timepoint(tp)
{}

basic_sample::basic_sample(timepoint_t&& tp) :
    _timepoint(std::move(tp))
{}

duration_t nrgprf::operator-(const basic_sample& lhs, const basic_sample& rhs)
{
    return std::chrono::duration_cast<duration_t>(lhs.timepoint() - rhs.timepoint());
}
