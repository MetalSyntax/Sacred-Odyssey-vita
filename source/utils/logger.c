/*
 * Copyright (C) 2022-2024 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "utils/logger.h"

#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/rtc.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/sysmodule.h>

#include <stdbool.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#define COLOR_RED    "\x1B[38;5;196m"
#define COLOR_PINK   "\x1B[38;5;212m"
#define COLOR_ORANGE "\x1B[38;5;202m"
#define COLOR_BLUE   "\x1B[38;5;32m"
#define COLOR_GREEN  "\x1B[32m"
#define COLOR_CYAN   "\x1B[36m"

#define COLOR_END    "\033[0m"

// Matches psvita-toolkit's `logs-live` (debugnet_server.py): one plain-text
// UTF-8 line per UDP datagram, broadcast so no PC IP needs to be configured
// on-device, default port 9999. Severity is conveyed as a bracketed marker
// in the text itself (e.g. "[ERROR]"), not a binary field.
#define LOG_UDP_PORT       9999
#define LOG_UDP_BROADCAST  "255.255.255.255"
#define LOG_NET_POOL_SIZE  (1 * 1024 * 1024)

static SceKernelLwMutexWork _log_mutex;
static atomic_bool _log_mutex_ready = ATOMIC_VAR_INIT(false);

// Buffer A is used to adjust the format string.
static char buffer_a[2048];
// Buffer B is used to compile the final log using the updated format string.
static char buffer_b[2048];
// Plain (no ANSI color, no unicode bullet) line shared by the file and UDP
// sinks, tagged with a bracketed severity marker.
static char buffer_plain[2048];
// Scratch buffer for the "(repeated Nx)" annotation added to periodic
// reminders of a throttled repeat run (see _log_should_write_sinks below).
static char buffer_repeat[2080];

// Some engine subsystems (this port's own patched .so included) log the same
// handful of distinct lines hundreds of times in a tight burst -- e.g. a
// material-binding pass over many materials that all miss the same uniform
// names ("invalid bind symbol: DiffuseColor"/"Sampler0"/"TextureMatrix0"/...,
// confirmed by the hundreds of repeats in log_20260906_225550.txt and
// log_20260909_214145.txt). The console print (sceClibPrintf, below) is
// local/cheap and stays unthrottled; but _log_write_file() does a fresh
// sceIoOpen+Write+Close per line and _log_send_udp() a fresh sendto per line,
// and paying that per-line disk+network cost hundreds of times for BYTE-FOR-
// BYTE identical text is pure overhead that measurably slows down whatever
// engine code is chattily logging (observed: the game stalls on a single
// frame for the whole duration of such a burst, since nothing else runs
// until the logging engine call returns).
// This throttles ONLY the file/UDP sinks, and ONLY for a message that is a
// byte-for-byte repeat of one already durably recorded at least 3 times --
// it never delays or drops the FIRST appearance of any line (so a crash
// right after a brand-new line still has it on disk, per the per-line-open
// discipline above), and periodic reminders (every 500th repeat) keep the
// file/UDP timeline readable instead of silently going quiet.
// 64 was sized for a quiet menu session. A world load (log_20260911_171008.txt:
// LoadWorld() world 10, "World loading: game objects") burns through this table in
// the first ~15 objects -- 107 "Loading game object: <UniqueName>..." lines (each
// legitimately distinct, one per game object) plus dozens of distinct fopen(...)
// DEBUG paths fill every slot with one-off strings that were never going to repeat
// anyway, before the genuinely repetitive material-bind spam (2688 "invalid bind
// symbol"/"Unused parameter" lines across that one log, e.g. "ambientcolor",
// "BoneMatrices", "Sampler2" -- the SAME handful of strings once per material) ever
// gets a tracking slot. Once the table is full, _log_throttle_sinks() fails safe by
// never throttling anything new -- so for the rest of that load, every recurrence of
// those handful of strings pays a fresh sceIoOpen+Write+Close and sendto, in a single
// blocking World::LoadMap() call that never swaps a frame in between (confirmed: the
// [render_diag] frame counter in that log stops dead the instant LoadWorld() starts
// and never appears again for the following ~9700 lines). 4096 slots (32KB static,
// uint32_t hash + uint32_t count each) comfortably covers a whole level's worth of
// distinct lines so the actually-repetitive ones get caught and throttled instead of
// being crowded out by one-off object/file names.
#define LOG_REPEAT_TABLE_SIZE 4096
#define LOG_REPEAT_FULL_EVERY 3
#define LOG_REPEAT_REMINDER_EVERY 500
static uint32_t _log_repeat_hash[LOG_REPEAT_TABLE_SIZE];
static uint32_t _log_repeat_count[LOG_REPEAT_TABLE_SIZE];
static int _log_repeat_used = 0;

static uint32_t _log_fnv1a(const char *s) {
    uint32_t h = 2166136261u;
    while (*s) {
        h ^= (unsigned char)*s++;
        h *= 16777619u;
    }
    return h;
}

// Returns the line to actually write to the file/UDP sinks (either `line`
// unchanged, or NULL to suppress this occurrence entirely).
static const char *_log_throttle_sinks(const char *line) {
    uint32_t h = _log_fnv1a(line);

    for (int i = 0; i < _log_repeat_used; i++) {
        if (_log_repeat_hash[i] == h) {
            uint32_t n = ++_log_repeat_count[i];
            if (n <= LOG_REPEAT_FULL_EVERY) {
                return line;
            }
            if (n % LOG_REPEAT_REMINDER_EVERY == 0) {
                size_t len = strlen(line);
                // line already ends with '\n' -- splice the note before it.
                if (len > 0 && len < sizeof(buffer_repeat) - 32) {
                    sceClibMemcpy(buffer_repeat, line, len - 1);
                    sceClibSnprintf(buffer_repeat + len - 1,
                                    sizeof(buffer_repeat) - (len - 1),
                                    " (repeated %ux)\n", n);
                    return buffer_repeat;
                }
            }
            return NULL;
        }
    }

    // Never seen before (or the small table is full -- fail safe by never
    // throttling instead of evicting/misattributing counts): log it in full
    // and start tracking it if there's room.
    if (_log_repeat_used < LOG_REPEAT_TABLE_SIZE) {
        _log_repeat_hash[_log_repeat_used] = h;
        _log_repeat_count[_log_repeat_used] = 1;
        _log_repeat_used++;
    }
    return line;
}

// One log file per execution (per hardware_debugging.md), opened/written/
// closed on every single line so a crash right after a log call doesn't
// lose it -- no buffering to flush.
static char _log_file_path[256] = {0};
static atomic_bool _log_file_ready = ATOMIC_VAR_INIT(false);

static int _log_udp_socket = -1;
static SceNetSockaddrIn _log_udp_addr;
static void *_log_net_pool = NULL;
static atomic_bool _log_net_ready = ATOMIC_VAR_INIT(false);

static const char *_level_tag(int t) {
    switch (t) {
        case LT_DEBUG:   return "[DEBUG]";
        case LT_INFO:    return "[INFO]";
        case LT_WARN:    return "[WARN]";
        case LT_ERROR:   return "[ERROR]";
        case LT_FATAL:   return "[FATAL]";
        case LT_SUCCESS: return "[SUCCESS]";
        case LT_WAIT:    return "[WAIT]";
        default:         return "[LOG]";
    }
}

static void _log_file_init(void) {
    sceIoMkdir(DATA_PATH "logs", 0777);

    SceDateTime t;
    sceRtcGetCurrentClockLocalTime(&t);
    sceClibSnprintf(_log_file_path, sizeof(_log_file_path),
                     DATA_PATH "logs/log_%04u%02u%02u_%02u%02u%02u.txt",
                     t.year, t.month, t.day, t.hour, t.minute, t.second);

    atomic_store_explicit(&_log_file_ready, true, memory_order_relaxed);
}

static void _log_write_file(const char *line) {
    if (!atomic_load_explicit(&_log_file_ready, memory_order_relaxed)) {
        _log_file_init();
    }

    SceUID fd = sceIoOpen(_log_file_path,
                          SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0777);
    if (fd >= 0) {
        sceIoWrite(fd, line, strlen(line));
        sceIoClose(fd);
    }
}

static void _log_net_init(void) {
    // Best-effort: if there's no network (airplane mode, no Wi-Fi), this
    // just leaves _log_udp_socket at -1 and every future call becomes a
    // no-op check -- it never blocks or fatals logging over it.
    //
    // Unlike SceCtrl/SceDisplay/etc., SceNet isn't auto-loaded -- calling
    // any sceNet*/sceNetCtl* function before this returns jumps through an
    // unresolved (NULL) stub and crashes with PC=0.
    if (sceSysmoduleIsLoaded(SCE_SYSMODULE_NET) != SCE_SYSMODULE_LOADED) {
        if (sceSysmoduleLoadModule(SCE_SYSMODULE_NET) < 0) {
            atomic_store_explicit(&_log_net_ready, true, memory_order_relaxed);
            return;
        }
    }

    sceNetCtlInit();

    _log_net_pool = malloc(LOG_NET_POOL_SIZE);
    if (_log_net_pool) {
        SceNetInitParam param = {
            .memory = _log_net_pool,
            .size = LOG_NET_POOL_SIZE,
            .flags = 0
        };
        sceNetInit(&param);
    }

    int s = sceNetSocket("game_log_udp", SCE_NET_AF_INET, SCE_NET_SOCK_DGRAM, 0);
    if (s >= 0) {
        int enable = 1;
        sceNetSetsockopt(s, SCE_NET_SOL_SOCKET, SCE_NET_SO_BROADCAST,
                         &enable, sizeof(enable));

        sceClibMemset(&_log_udp_addr, 0, sizeof(_log_udp_addr));
        _log_udp_addr.sin_family = SCE_NET_AF_INET;
        _log_udp_addr.sin_port = sceNetHtons(LOG_UDP_PORT);
        sceNetInetPton(SCE_NET_AF_INET, LOG_UDP_BROADCAST, &_log_udp_addr.sin_addr);

        _log_udp_socket = s;
    }

    atomic_store_explicit(&_log_net_ready, true, memory_order_relaxed);
}

static void _log_send_udp(const char *line) {
    if (!atomic_load_explicit(&_log_net_ready, memory_order_relaxed)) {
        _log_net_init();
    }

    if (_log_udp_socket >= 0) {
        sceNetSendto(_log_udp_socket, line, strlen(line), 0,
                     (SceNetSockaddr *) &_log_udp_addr, sizeof(_log_udp_addr));
    }
}

void _log_print(int t, const char* fmt, ...) {
    if (!atomic_load_explicit(&_log_mutex_ready, memory_order_relaxed)) {
        int ret = sceKernelCreateLwMutex(&_log_mutex, "log_lock", 0, 0, NULL);
        if (ret < 0) {
            sceClibPrintf("Error: failed to create log mutex: 0x%x\n", ret);
            return;
        }
        atomic_store_explicit(&_log_mutex_ready, true, memory_order_relaxed);
    }
    sceKernelLockLwMutex(&_log_mutex, 1, NULL);

    switch (t) {
        case LT_DEBUG:
            sceClibSnprintf(buffer_a, sizeof(buffer_a), " %s• debug%s    %s\n",
                            COLOR_PINK, COLOR_END, fmt); break;
        case LT_INFO:
            sceClibSnprintf(buffer_a, sizeof(buffer_a), " %sℹ info%s     %s\n",
                            COLOR_BLUE, COLOR_END, fmt); break;
        case LT_WARN:
            sceClibSnprintf(buffer_a, sizeof(buffer_a), " %s⚠ warning%s  %s\n",
                            COLOR_ORANGE, COLOR_END, fmt); break;
        case LT_ERROR:
            sceClibSnprintf(buffer_a, sizeof(buffer_a), " %s⨯ error%s    %s\n",
                            COLOR_RED, COLOR_END, fmt); break;
        case LT_FATAL:
            sceClibSnprintf(buffer_a, sizeof(buffer_a), " %s! fatal%s    %s\n",
                            COLOR_RED, COLOR_END, fmt); break;
        case LT_SUCCESS:
            sceClibSnprintf(buffer_a, sizeof(buffer_a), " %s! success%s  %s\n",
                            COLOR_GREEN, COLOR_END, fmt); break;
        case LT_WAIT:
            sceClibSnprintf(buffer_a, sizeof(buffer_a), " %s… waiting%s  %s\n",
                            COLOR_CYAN, COLOR_END, fmt); break;
        default:
            if (atomic_load_explicit(&_log_mutex_ready, memory_order_relaxed)) {
                sceKernelUnlockLwMutex(&_log_mutex, 1);
            }
            return;
    }

    va_list list;
    va_start(list, fmt);
    sceClibVsnprintf(buffer_b, sizeof(buffer_b), buffer_a, list);
    va_end(list);
    // NEVER pass buffer_b itself as the format string here: it's the already-substituted
    // text of a caller-provided message, and can legitimately contain a literal '%' (e.g.
    // the new fmt.c tracing logs raw format strings like "fmt=\"%s\"" or "fmt=\"\t%s\"" as
    // DATA). Passing it straight to sceClibPrintf() re-interprets that literal text as a
    // SECOND format string, and sceClibPrintf() then reads a nonexistent vararg for the
    // phantom "%s" -- garbage off the stack, dereferenced as a string. Confirmed on real
    // hardware: this crashed inside SceLibKernel every few calls once fmt.c started logging
    // "\t%s"-style format strings for each GL extension token (dump
    // sacredodyssey-psp2core-1788544751-...), never before because no logged message
    // happened to contain a literal '%' until then.
    sceClibPrintf("%s", buffer_b);

    va_start(list, fmt);
    sceClibVsnprintf(buffer_a, sizeof(buffer_a), fmt, list);
    va_end(list);
    sceClibSnprintf(buffer_plain, sizeof(buffer_plain), "%s %s\n",
                     _level_tag(t), buffer_a);
    const char *to_sinks = _log_throttle_sinks(buffer_plain);
    if (to_sinks) {
        _log_write_file(to_sinks);
        _log_send_udp(to_sinks);
    }

    if (atomic_load_explicit(&_log_mutex_ready, memory_order_relaxed)) {
        sceKernelUnlockLwMutex(&_log_mutex, 1);
    }
}
