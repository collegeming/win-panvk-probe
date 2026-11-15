/* Minimal Mesa c11/threads.h replacement.  SPDX-License-Identifier: MIT */
#ifndef VDRM_COMPAT_C11_THREADS_H
#define VDRM_COMPAT_C11_THREADS_H

#include "util/simple_mtx.h"

static inline int
thrd_yield(void)
{
   SwitchToThread();
   return 0;
}

#endif /* VDRM_COMPAT_C11_THREADS_H */
