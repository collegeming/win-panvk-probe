/*
 * Minimal Mesa util/simple_mtx.h replacement for the standalone probe.
 *
 * The probe links the upstream MIT-licensed vdrm transport sources unmodified
 * apart from probe diagnostics.  Those files expect a handful of Mesa utility
 * headers; this directory supplies the smallest possible implementations on
 * top of Win32 so the probe does not need a Mesa tree or meson build.
 *
 * Not a Mesa source file.  SPDX-License-Identifier: MIT
 */
#ifndef VDRM_COMPAT_SIMPLE_MTX_H
#define VDRM_COMPAT_SIMPLE_MTX_H

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

typedef struct simple_mtx_t {
   CRITICAL_SECTION cs;
} simple_mtx_t;

#ifndef mtx_plain
#define mtx_plain 0
#endif

static inline void
simple_mtx_init(simple_mtx_t *mtx, int type)
{
   (void)type;
   InitializeCriticalSection(&mtx->cs);
}

static inline void
simple_mtx_lock(simple_mtx_t *mtx)
{
   EnterCriticalSection(&mtx->cs);
}

static inline void
simple_mtx_unlock(simple_mtx_t *mtx)
{
   LeaveCriticalSection(&mtx->cs);
}

static inline void
simple_mtx_destroy(simple_mtx_t *mtx)
{
   DeleteCriticalSection(&mtx->cs);
}

/* vdrm.c/vdrm_wddm.c only call this as a debug assertion.  A no-op is safe:
 * every call site is already inside the matching lock. */
static inline void
simple_mtx_assert_locked(simple_mtx_t *mtx)
{
   (void)mtx;
}

#endif /* VDRM_COMPAT_SIMPLE_MTX_H */
