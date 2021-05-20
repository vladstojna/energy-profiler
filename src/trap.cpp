// trap.cpp

#include "trap.hpp"
#include "sampler.hpp"

using namespace tep;


template<typename R>
unbounded_creator<R>::unbounded_creator(const R* reader,
    const std::chrono::milliseconds& period,
    size_t initial_size) :
    _reader(reader),
    _period(period),
    _initsz(initial_size)
{}

template<typename R>
std::unique_ptr<async_sampler> unbounded_creator<R>::create() const
{
    return std::make_unique<unbounded_ps>(_reader, _initsz, _period);
}


template<typename R>
bounded_creator<R>::bounded_creator(const R* reader, const std::chrono::milliseconds& period) :
    _reader(reader),
    _period(period)
{}

template<typename R>
std::unique_ptr<async_sampler> bounded_creator<R>::create() const
{
    return std::make_unique<bounded_ps>(_reader, _period);
}


trap_data::trap_data(uintptr_t addr, long ow, std::unique_ptr<sampler_creator>&& creator) :
    _addr(addr),
    _origw(ow),
    _creator(std::move(creator))
{}

uintptr_t trap_data::address() const
{
    return _addr;
}

long trap_data::original_word() const
{
    return _origw;
}

std::unique_ptr<async_sampler> trap_data::create_sampler() const
{
    return _creator->create();
}


bool tep::operator<(const trap_data& lhs, const trap_data& rhs)
{
    return lhs.address() < rhs.address();
}

bool tep::operator<(uintptr_t lhs, const trap_data& rhs)
{
    return lhs < rhs.address();
}

bool tep::operator<(const trap_data& lhs, uintptr_t rhs)
{
    return lhs.address() < rhs;
}


// explicit template instantiation

template
class unbounded_creator<nrgprf::reader_rapl>;

template
class unbounded_creator<nrgprf::reader_gpu>;

template
class bounded_creator<nrgprf::reader_rapl>;

template
class bounded_creator<nrgprf::reader_gpu>;
