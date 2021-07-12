// arch.hpp

#pragma once

#if !defined(NRG_PPC64) && !defined(NRG_X86_64)

#if defined(__powerpc64__)
#define NRG_PPC64
#elif defined(__x86_64__) || defined(__i386__)
#define NRG_X86_64
#else
#error Unsupported architecture detected
#endif

#endif

#if defined(NRG_PPC64) && defined(NRG_X86_64)
#error NRG_PPC64 and NRG_X86_64 both defined
#endif
