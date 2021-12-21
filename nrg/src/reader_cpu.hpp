#pragma once

#include <nrg/arch.hpp>

#if defined(CPU_NONE)
#include "none/reader_cpu.hpp"
#elif defined(NRG_X86_64)
#include "x86_64/reader_cpu.hpp"
#elif defined(NRG_PPC64)
#include "ppc64/reader_cpu.hpp"
#else
#error No CPU architecture defined
#endif // defined(CPU_NONE)
