/*
 * EHypView runtime for TOS, taken from ECal (github.com/whomper/ECal),
 * which took it from EMail (whomper/EMAIL) and Claude ST (whomper/atari_claude).
 * Copyright (C) the EHypView, ECal, EMail and Claude ST authors.
 * Free software under the GNU General Public License, version 2 or
 * (at your option) any later version; see COPYING.
 */
/* EMail freestanding libc: formatted output into strings only */
#ifndef _STDIO_H
#define _STDIO_H
#include <stddef.h>
#include <stdarg.h>
int vsnprintf(char *buf, size_t n, const char *fmt, va_list ap);
int snprintf(char *buf, size_t n, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
int sprintf(char *buf, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
#endif
