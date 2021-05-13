// reader.hpp

#pragma once

#include <cstddef>
#include <cstdint>

namespace nrgprf
{

    class error;
    class sample;

    class reader
    {
    protected:
        ~reader() = default;

    public:
        virtual error read(sample& s) const = 0;
        virtual error read(sample& s, uint8_t ev_idx) const = 0;
        virtual size_t num_events() const = 0;
    };

}
