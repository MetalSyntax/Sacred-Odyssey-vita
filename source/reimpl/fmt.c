/*
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "reimpl/fmt.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "utils/logger.h"

// Confirmed on real hardware (dump sacredodyssey-psp2core-1788539515-...): the .so calls
// sprintf() internally (e.g. GameSettings::getDefault*File()'s "%s%s" path-building calls)
// with a %s argument that can be an invalid pointer (0xfffffff8 observed) coming from a
// virtual call whose result we haven't been able to pin down statically. Crashing inside
// strlen()/_svfprintf_r() there takes down the whole loader before the main menu ever shows.
// Rather than guess which specific call site is at fault, we make the whole *printf family
// the .so can reach defensive: walk the format string ourselves (consuming va_list args with
// the same types/sizes the real vfprintf would, so alignment for later %s conversions stays
// correct) and refuse to hand off to the real snprintf/vsprintf if any %s argument looks like
// a bad pointer. On a real/valid call this adds a cheap read-only scan and then defers 100%
// to the real implementation -- behavior for every already-working sprintf call in this
// project (paths, filenames, etc.) is unchanged.
static bool is_pointer_plausible(const void *p) {
    uintptr_t v = (uintptr_t) p;
    if (v == 0) return false;
    if (v < 0x1000) return false;
    // Catches -1/-8/etc-style sentinel values misused as a jstring/char* (e.g. 0xfffffff8),
    // which show up as huge addresses when reinterpreted as unsigned pointers. Nothing in
    // this port's address space (loader, .so, heap) lives up near the top of the 32-bit range.
    if (v > 0xf0000000u) return false;
    return true;
}

// Returns false (format rejected) the moment a %s argument fails is_pointer_plausible().
// `ap` must be a va_list dedicated to this scan (va_copy'd by the caller) -- it is consumed
// and must not be reused afterwards.
static bool scan_format_args(const char *fmt, va_list ap) {
    const char *p = fmt;
    while (*p) {
        if (*p != '%') {
            p++;
            continue;
        }
        p++;
        if (*p == '%') {
            p++;
            continue;
        }

        while (*p == '-' || *p == '+' || *p == ' ' || *p == '#' || *p == '0') p++;

        if (*p == '*') {
            (void) va_arg(ap, int);
            p++;
        } else {
            while (*p >= '0' && *p <= '9') p++;
        }

        if (*p == '.') {
            p++;
            if (*p == '*') {
                (void) va_arg(ap, int);
                p++;
            } else {
                while (*p >= '0' && *p <= '9') p++;
            }
        }

        int length = 0; // 0=int-sized, 1=long-sized, 2=long long-sized
        if (*p == 'h') {
            p++;
            if (*p == 'h') p++;
            // char/short are promoted to int in variadic calls -- no length change.
        } else if (*p == 'l') {
            p++;
            if (*p == 'l') {
                p++;
                length = 2;
            } else {
                length = 1;
            }
        } else if (*p == 'L') {
            p++;
            length = 1; // best-effort: treat long double like a long-sized slot below
        } else if (*p == 'z' || *p == 'j' || *p == 't' || *p == 'q') {
            p++;
            length = 1; // size_t/intmax_t/ptrdiff_t are 32-bit here, same slot as long
        }

        if (!*p) break;
        char conv = *p++;

        switch (conv) {
            case 'd': case 'i': case 'o': case 'u': case 'x': case 'X': case 'c':
                if (length == 2) (void) va_arg(ap, long long);
                else if (length == 1) (void) va_arg(ap, long);
                else (void) va_arg(ap, int);
                break;
            case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': case 'a': case 'A':
                (void) va_arg(ap, double);
                break;
            case 'p': case 'n':
                (void) va_arg(ap, void *);
                break;
            case 's': {
                const char *s = va_arg(ap, const char *);
                if (!is_pointer_plausible(s)) {
                    return false;
                }
                break;
            }
            default:
                // Unknown/unsupported specifier -- stop scanning rather than risk
                // misinterpreting the remaining args; let the real implementation
                // deal with it (already-working calls never hit this branch).
                return true;
        }
    }
    return true;
}

int sprintf_soloader(char *buf, const char *fmt, ...) {
    // NOTE: this used to unconditionally l_debug() every fmt string here (added to catch a
    // %s-pointer crash whose call site scan_format_args() below didn't statically pin down).
    // That crash guard below is unconditional and independent of logging -- it still runs and
    // still blocks bad calls. But the trace line itself, called from every single sprintf the
    // .so makes (thousands during scene loads), was confirmed to be the single largest
    // contributor to the multi-minute stall loading mainmenu2 (log_20260910_013817.txt: 1914
    // of the ~5067 log lines emitted during that one stall were these four functions' traces --
    // each one a synchronous flash open+write+close plus a UDP send). Removed for load speed;
    // the "blocked call" l_warn below still fires and still gets the offending fmt to disk/UDP
    // if scan_format_args() ever rejects one again.
    va_list ap, ap_scan;
    va_start(ap, fmt);
    va_copy(ap_scan, ap);
    bool ok = scan_format_args(fmt, ap_scan);
    va_end(ap_scan);

    if (!ok) {
        l_warn("sprintf_soloader: blocked call with an invalid %%s pointer, fmt=\"%s\"", fmt);
        buf[0] = '\0';
        va_end(ap);
        return 0;
    }

    int ret = vsprintf(buf, fmt, ap);
    va_end(ap);
    return ret;
}

int snprintf_soloader(char *buf, size_t size, const char *fmt, ...) {
    va_list ap, ap_scan;
    va_start(ap, fmt);
    va_copy(ap_scan, ap);
    bool ok = scan_format_args(fmt, ap_scan);
    va_end(ap_scan);

    if (!ok) {
        l_warn("snprintf_soloader: blocked call with an invalid %%s pointer, fmt=\"%s\"", fmt);
        if (size > 0) buf[0] = '\0';
        va_end(ap);
        return 0;
    }

    int ret = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return ret;
}

int vsprintf_soloader(char *buf, const char *fmt, va_list ap) {
    va_list ap_scan;
    va_copy(ap_scan, ap);
    bool ok = scan_format_args(fmt, ap_scan);
    va_end(ap_scan);

    if (!ok) {
        l_warn("vsprintf_soloader: blocked call with an invalid %%s pointer, fmt=\"%s\"", fmt);
        buf[0] = '\0';
        return 0;
    }

    return vsprintf(buf, fmt, ap);
}

int vsnprintf_soloader(char *buf, size_t size, const char *fmt, va_list ap) {
    va_list ap_scan;
    va_copy(ap_scan, ap);
    bool ok = scan_format_args(fmt, ap_scan);
    va_end(ap_scan);

    if (!ok) {
        l_warn("vsnprintf_soloader: blocked call with an invalid %%s pointer, fmt=\"%s\"", fmt);
        if (size > 0) buf[0] = '\0';
        return 0;
    }

    return vsnprintf(buf, size, fmt, ap);
}
