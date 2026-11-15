/* Minimal Mesa util/u_debug.h replacement.  SPDX-License-Identifier: MIT */
#ifndef VDRM_COMPAT_U_DEBUG_H
#define VDRM_COMPAT_U_DEBUG_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static inline bool
debug_get_bool_option(const char *name, bool dflt)
{
   const char *s = getenv(name);

   if (!s || !*s)
      return dflt;
   if (!strcmp(s, "0") || !strcmp(s, "false") || !strcmp(s, "no") ||
       !strcmp(s, "off"))
      return false;
   return true;
}

static inline int64_t
debug_get_num_option(const char *name, int64_t dflt)
{
   const char *s = getenv(name);
   char *end = NULL;
   long long v;

   if (!s || !*s)
      return dflt;
   v = _strtoi64(s, &end, 0);
   if (end == s)
      return dflt;
   return (int64_t)v;
}

/* Mesa caches these; the probe is short lived and re-reads the environment,
 * which also lets a single process exercise two connection rounds. */
#define DEBUG_GET_ONCE_BOOL_OPTION(name, env, dflt)                          \
   static inline bool debug_get_option_##name(void)                          \
   {                                                                         \
      return debug_get_bool_option(env, dflt);                               \
   }

#define DEBUG_GET_ONCE_NUM_OPTION(name, env, dflt)                           \
   static inline int64_t debug_get_option_##name(void)                       \
   {                                                                         \
      return debug_get_num_option(env, dflt);                                \
   }

#endif /* VDRM_COMPAT_U_DEBUG_H */
