// hybrid_reader.cpp

#include <nrg/hybrid_reader.hpp>
#include <nrg/error.hpp>
#include <nrg/sample.hpp>

#include <nonstd/expected.hpp>

#include <cassert>

using namespace nrgprf;

void hybrid_reader::push_back(const reader& r)
{
    _readers.push_back(&r);
}

error hybrid_reader::read(sample& s) const
{
    for (auto r : _readers)
    {
        assert(r != nullptr);
        error err = r->read(s);
        if (err)
            return err;
    }
    return error::success();
}

error hybrid_reader::read(sample&, uint8_t) const
{
    return error(error_code::NOT_IMPL, "Reading specific events not supported");
}

result<sample> hybrid_reader::read() const
{
    sample s;
    if (error err = read(s))
        return result<sample>{ nonstd::unexpect, std::move(err) };
    return s;
}

result<sample> hybrid_reader::read(uint8_t) const
{
    return result<sample>{ nonstd::unexpect,
        error_code::NOT_IMPL,
        "Reading specific events not supported" };
}

size_t hybrid_reader::num_events() const
{
    size_t total = 0;
    for (auto r : _readers)
    {
        assert(r != nullptr);
        total += r->num_events();
    }
    return total;
}
