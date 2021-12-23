#pragma once

#if defined(GPU_NONE)
#undef GPU_NONE
#endif

#if !defined(GPU_NV) && !defined(GPU_AMD)
#define GPU_NONE
#endif

#if defined(GPU_NONE)
#include "none/reader_gpu.hpp"
#elif defined(GPU_NV) || defined(GPU_AMD)
#include "common/gpu/reader.hpp"
#else
#error No GPU vendor defined
#endif // defined(GPU_NONE)
