/* Minimal Mesa util/u_math.h replacement.  SPDX-License-Identifier: MIT */
#ifndef VDRM_COMPAT_U_MATH_H
#define VDRM_COMPAT_U_MATH_H

#include <stddef.h>
#include <stdint.h>

/* vdrm.c calls align(size, 8); both arguments are integer expressions. */
#ifndef align
#define align(x, a) (((x) + ((a) - 1)) & ~((a) - 1))
#endif

#endif /* VDRM_COMPAT_U_MATH_H */
