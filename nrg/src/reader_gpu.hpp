// reader_gpu.hpp

#pragma once

#include "error.hpp"
#include "event_gpu.hpp"
#include "sample.hpp"

#define GPU_NV

#if defined(GPU_NV)
typedef struct nvmlDevice_st* nvmlDevice_t;
#endif

namespace nrgprf
{

    class reader_gpu
    {
    private:
        uint8_t _dev_mask;
        event_gpu _events[MAX_SOCKETS];
        nvmlDevice_t _handles[MAX_SOCKETS];

    public:
        reader_gpu(uint8_t dev_mask, error& ec);
        ~reader_gpu() noexcept;

        // forbid copies
        reader_gpu(const reader_gpu& other) = delete;
        reader_gpu& operator=(const reader_gpu& other) = delete;

        const event_gpu& event(size_t dev) const;
        error read(sample& s) const;
    };

}
