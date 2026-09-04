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
    sceClibPrintf(buffer_b);

    va_start(list, fmt);
    sceClibVsnprintf(buffer_a, sizeof(buffer_a), fmt, list);
    va_end(list);
    sceClibSnprintf(buffer_plain, sizeof(buffer_plain), "%s %s\n",
                     _level_tag(t), buffer_a);
    _log_write_file(buffer_plain);
    _log_send_udp(buffer_plain);

    if (atomic_load_explicit(&_log_mutex_ready, memory_order_relaxed)) {
        sceKernelUnlockLwMutex(&_log_mutex, 1);
    }
}
