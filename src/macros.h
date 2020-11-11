// fileline.h
#pragma once

#if !defined(FILELINE)
#define FILELINE 1
#endif

#if FILELINE

#define tep_str__(x) #x
#define tep_stringify__(x) tep_str__(x)
#define fileline(str_literal) __FILE__ "@" tep_stringify__(__LINE__) ": " str_literal

#else

#define fileline(str_literal) str_literal

#endif

#if defined(NDEBUG)

#define dbg(x) do { } while (0)

#else

#define dbg(x) do { x; } while (0)

#endif
