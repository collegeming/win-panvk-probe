/* Minimal Mesa util/u_atomic.h replacement.  SPDX-License-Identifier: MIT */
#ifndef VDRM_COMPAT_U_ATOMIC_H
#define VDRM_COMPAT_U_ATOMIC_H

#include <stdint.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

/* The probe only uses these on 32-bit counters shared with its own threads.
 * InterlockedExchange supplies the release/acquire semantics Mesa relies on. */
#define p_atomic_read(ptr) (*(volatile uint32_t *)(ptr))
#define p_atomic_set(ptr, value) (*(volatile uint32_t *)(ptr) = (uint32_t)(value))

static inline uint32_t
p_atomic_xchg(uint32_t *ptr, uint32_t value)
{
   return (uint32_t)InterlockedExchange((volatile LONG *)ptr, (LONG)value);
}

#endif /* VDRM_COMPAT_U_ATOMIC_H */
