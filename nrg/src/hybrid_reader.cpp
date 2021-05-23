// hybrid_reader.cpp

#include <nrg/hybrid_reader.hpp>
#include <nrg/error.hpp>

using namespace nrgprf;

void hybrid_reader::push_back(const reader* r)
{
    _readers.push_back(r);
}

error hybrid_reader::read(sample& s) const
{
    for (auto r : _readers)
    {
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

size_t hybrid_reader::num_events() const
{
    size_t total = 0;
    for (auto r : _readers)
        total += r->num_events();
    return total;
}
