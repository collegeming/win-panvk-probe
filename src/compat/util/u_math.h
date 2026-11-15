/* Minimal Mesa util/u_math.h replacement.  SPDX-License-Identifier: MIT
 *
 * Deliberately does NOT define Mesa's two-argument `align(x, a)` macro: the
 * Windows SDK's DECLSPEC_ALIGN(x) expands to __declspec(align(x)), and a
 * function-like `align` macro is then rescanned with one argument, breaking
 * every winnt.h include.  The vendored vdrm.c uses vdrm_align() instead.
 */
#ifndef VDRM_COMPAT_U_MATH_H
#define VDRM_COMPAT_U_MATH_H

#include <stddef.h>
#include <stdint.h>

#define vdrm_align(x, a) (((x) + ((a) - 1)) & ~((a) - 1))

#endif /* VDRM_COMPAT_U_MATH_H */
