/* Minimal Mesa util/os_time.h replacement.  SPDX-License-Identifier: MIT */
#ifndef VDRM_COMPAT_OS_TIME_H
#define VDRM_COMPAT_OS_TIME_H

#include <stdint.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

/* Monotonic nanoseconds, used only for bounded waits and diagnostics. */
static inline int64_t
os_time_get_nano(void)
{
   static LARGE_INTEGER freq;
   LARGE_INTEGER now;

   if (freq.QuadPart == 0)
      QueryPerformanceFrequency(&freq);
   QueryPerformanceCounter(&now);
   return (int64_t)(now.QuadPart / freq.QuadPart) * 1000000000ll +
          (int64_t)((now.QuadPart % freq.QuadPart) * 1000000000ll / freq.QuadPart);
}

#endif /* VDRM_COMPAT_OS_TIME_H */
