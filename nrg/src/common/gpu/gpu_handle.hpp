#pragma once

#if defined(GPU_NV)
#include <nvml.h>
#elif defined(GPU_AMD)
#include <cstdint>
#endif // defined(GPU_NV)

namespace nrgprf {
#if defined(GPU_NV)
using gpu_handle = nvmlDevice_t;
#elif defined(GPU_AMD)
using gpu_handle = uint32_t;
#endif // defined(GPU_NV)
} // namespace nrgprf
