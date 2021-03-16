// reader_gpu.hpp

#pragma once

#include "error.hpp"
#include "reader_rapl.hpp"
#include "sample.hpp"

#define GPU_NV

#if defined(GPU_NV)
typedef struct nvmlDevice_st* nvmlDevice_t;
#endif

namespace nrgprf
{

    namespace detail
    {

        struct nvml_handle
        {
            nvml_handle(error& ec);
            ~nvml_handle() noexcept;

            nvml_handle(const nvml_handle& other) noexcept;
            nvml_handle(nvml_handle&& other) noexcept;

            nvml_handle& operator=(const nvml_handle& other) noexcept;
            nvml_handle& operator=(nvml_handle&& other) noexcept;
        };

    }

    class reader_gpu
    {
    private:
        detail::nvml_handle _nvml_handle;
        size_t _offset;
        int8_t _event_map[MAX_SOCKETS];
        std::vector<nvmlDevice_t> _active_handles;

    public:
        reader_gpu(error& ec);
        reader_gpu(const reader_rapl& reader, error& ec);
        reader_gpu(uint8_t dev_mask, error& ec);
        reader_gpu(uint8_t dev_mask, const reader_rapl& reader, error& ec);

        error read(sample& s) const;
        error read(sample& s, int8_t ev_idx) const;

        int8_t event_idx(uint8_t device) const;
        size_t num_events() const;

        result<uint64_t> get_board_power(const sample& s, uint8_t dev) const;

    private:
        reader_gpu(uint8_t dev_mask, size_t offset, error& ec);
    };

}
