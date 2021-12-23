#pragma once

#if __GNUC__ >= 4
#define NRG_PUBLIC __attribute__ ((visibility ("default")))
#define NRG_LOCAL  __attribute__ ((visibility ("hidden")))
#else
#define NRG_PUBLIC
#define NRG_LOCAL
#endif // __GNUC__ >= 4
