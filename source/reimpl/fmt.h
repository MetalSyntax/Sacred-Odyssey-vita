/*
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

/**
 * @file  fmt.h
 * @brief Crash-safe wrappers around the *printf family imported by the .so.
 */

#ifndef SACREDODYSSEY_REIMPL_FMT_H
#define SACREDODYSSEY_REIMPL_FMT_H

#include <stdarg.h>
#include <stddef.h>

int sprintf_soloader(char *buf, const char *fmt, ...);
int snprintf_soloader(char *buf, size_t size, const char *fmt, ...);
int vsprintf_soloader(char *buf, const char *fmt, va_list ap);
int vsnprintf_soloader(char *buf, size_t size, const char *fmt, va_list ap);

#endif // SACREDODYSSEY_REIMPL_FMT_H
