/* Minimal Mesa util/log.h replacement.  SPDX-License-Identifier: MIT */
#ifndef VDRM_COMPAT_LOG_H
#define VDRM_COMPAT_LOG_H

#include <stdarg.h>
#include <stdio.h>

static inline void
probe_vlog(const char *prefix, const char *fmt, va_list ap)
{
   fprintf(stderr, "%s", prefix);
   vfprintf(stderr, fmt, ap);
   fputc('\n', stderr);
   fflush(stderr);
}

static inline void
mesa_loge(const char *fmt, ...)
{
   va_list ap;
   va_start(ap, fmt);
   probe_vlog("error: ", fmt, ap);
   va_end(ap);
}

static inline void
mesa_logw(const char *fmt, ...)
{
   va_list ap;
   va_start(ap, fmt);
   probe_vlog("warn: ", fmt, ap);
   va_end(ap);
}

static inline void
mesa_logi(const char *fmt, ...)
{
   va_list ap;
   va_start(ap, fmt);
   probe_vlog("info: ", fmt, ap);
   va_end(ap);
}

static inline void
mesa_logd(const char *fmt, ...)
{
   va_list ap;
   va_start(ap, fmt);
   probe_vlog("debug: ", fmt, ap);
   va_end(ap);
}

#endif /* VDRM_COMPAT_LOG_H */
