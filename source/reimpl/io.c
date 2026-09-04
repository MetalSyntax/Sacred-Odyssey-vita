/*
 * Copyright (C) 2021      Andy Nguyen
 * Copyright (C) 2022      Rinnegatamante
 * Copyright (C) 2022-2024 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "reimpl/io.h"

#include <string.h>
#include <sys/stat.h>
#include <sys/unistd.h>
#include <stdlib.h>
#include <dirent.h>
#include <stdarg.h>
#include <psp2/kernel/threadmgr.h>

#ifdef USE_SCELIBC_IO
#include <libc_bridge/libc_bridge.h>
#endif

#include "utils/logger.h"
#include "utils/utils.h"

// Includes the following inline utilities:
// int oflags_musl_to_newlib(int flags);
// dirent64_bionic * dirent_newlib_to_bionic(struct dirent* dirent_newlib);
// void stat_newlib_to_bionic(struct stat * src, stat64_bionic * dst);
#include "reimpl/bits/_struct_converters.c"

FILE * fopen_soloader(const char * filename, const char * mode) {
    if (strcmp(filename, "/proc/cpuinfo") == 0) {
        return fopen_soloader("app0:/cpuinfo", mode);
    } else if (strcmp(filename, "/proc/meminfo") == 0) {
        return fopen_soloader("app0:/meminfo", mode);
    }

#ifdef USE_SCELIBC_IO
    FILE* ret = sceLibcBridge_fopen(filename, mode);
#else
    FILE* ret = fopen(filename, mode);
#endif

    // Guard against redirecting a path that's already one of our own fallback targets
    if (!ret && strncmp(filename, "app0:", 5) != 0 && strstr(filename, "PinkBadShader") == NULL) {
        static const char fallback_vp_src[] =
            "attribute highp   vec4 Vertex;\n"
            "uniform   mediump mat4 WorldViewProjectionMatrix;\n"
            "void main(void)\n"
            "{\n"
            "    gl_Position = WorldViewProjectionMatrix * Vertex;\n"
            "}\n";

        static const char fallback_fp_src[] =
            "precision mediump float;\n"
            "void main(void)\n"
            "{\n"
            "    gl_FragColor = vec4(0.8, 0.3, 0.5, 1.0);\n"
            "}\n";

        if (strstr(filename, "/effects/") && strstr(filename, ".glsl")) {
            const char *slash = strrchr(filename, '/');
            const char *base = slash ? slash + 1 : filename;
            // Check if vertex shader (VS.glsl, VP.glsl, Vertex.glsl) or fragment shader (PS.glsl, FP.glsl, etc.)
            bool is_vp = (strstr(base, "VP.glsl") != NULL ||
                          strstr(base, "VS.glsl") != NULL ||
                          strstr(base, "Vertex.glsl") != NULL);

            const char *fb_path = is_vp ? DATA_PATH "PinkBadShaderVP.glsl"
                                        : DATA_PATH "PinkBadShaderFP.glsl";
            FILE *chk = fopen(fb_path, "r");
            if (chk) {
                fclose(chk);
            } else {
                FILE *w = fopen(fb_path, "w");
                if (w) {
                    fputs(is_vp ? fallback_vp_src : fallback_fp_src, w);
                    fclose(w);
                    l_info("Created fallback shader: %s", fb_path);
                }
            }

            l_warn("Missing effect shader %s -- redirecting to fallback %s", filename, fb_path);
            ret = fopen_soloader(fb_path, mode);
        }
        // NOTE: glsl.config is deliberately NOT redirected here. Confirmed on
        // real hardware (comparing log_20260903_234632.txt against every log
        // after this redirect was added) that glitch::video::
        // CGLSLShaderManager::initAdditionalConfig() already handles this file
        // being missing (NULL) just fine and proceeds straight to compiling
        // shaders -- the "does not return" tag Ghidra puts on that branch is a
        // decompiler artifact, not a real hang. Serving an EMPTY substitute
        // file instead routes the engine through its "file exists" path
        // (size/alloc/read/second cleanup call) that normally never executes
        // for this project's data set -- THAT path is what actually hangs
        // (confirmed: every run with this redirect stops right after opening
        // the substitute file and never reaches glCompileShader; every run
        // without it reaches glCompileShader fine with glsl.config staying
        // NULL). Do not reintroduce this redirect without new evidence.
    }

    if (ret)
        l_debug("fopen(%s, %s): %p", filename, mode, ret);
    else
        l_warn("fopen(%s, %s): %p", filename, mode, ret);

    return ret;
}

int open_soloader(const char * path, int oflag, ...) {
    if (strcmp(path, "/proc/cpuinfo") == 0) {
        return open_soloader("app0:/cpuinfo", oflag);
    } else if (strcmp(path, "/proc/meminfo") == 0) {
        return open_soloader("app0:/meminfo", oflag);
    }

    mode_t mode = 0666;
    if (((oflag & BIONIC_O_CREAT) == BIONIC_O_CREAT) ||
        ((oflag & BIONIC_O_TMPFILE) == BIONIC_O_TMPFILE)) {
        va_list args;
        va_start(args, oflag);
        mode = (mode_t)(va_arg(args, int));
        va_end(args);
    }

    oflag = oflags_bionic_to_newlib(oflag);
    int ret = open(path, oflag, mode);
    if (ret >= 0)
        l_debug("open(%s, %x): %i", path, oflag, ret);
    else
        l_warn("open(%s, %x): %i", path, oflag, ret);
    return ret;
}

int fstat_soloader(int fd, stat64_bionic * buf) {
    struct stat st;
    int res = fstat(fd, &st);

    if (res == 0)
        stat_newlib_to_bionic(&st, buf);

    l_debug("fstat(%i): %i", fd, res);
    return res;
}

int stat_soloader(const char * path, stat64_bionic * buf) {
    struct stat st;
    int res = stat(path, &st);

    if (res == 0)
        stat_newlib_to_bionic(&st, buf);

    l_debug("stat(%s): %i", path, res);
    return res;
}

int fclose_soloader(FILE * f) {
#ifdef USE_SCELIBC_IO
    int ret = sceLibcBridge_fclose(f);
#else
    int ret = fclose(f);
#endif

    l_debug("fclose(%p): %i", f, ret);
    return ret;
}

int close_soloader(int fd) {
    int ret = close(fd);
    l_debug("close(%i): %i", fd, ret);
    return ret;
}

DIR* opendir_soloader(char* _pathname) {
    DIR* ret = opendir(_pathname);
    l_debug("opendir(\"%s\"): %p", _pathname, ret);
    return ret;
}

struct dirent64_bionic * readdir_soloader(DIR * dir) {
    static struct dirent64_bionic dirent_tmp;

    struct dirent* ret = readdir(dir);
    l_debug("readdir(%p): %p", dir, ret);

    if (ret) {
        dirent64_bionic* entry_tmp = dirent_newlib_to_bionic(ret);
        memcpy(&dirent_tmp, entry_tmp, sizeof(dirent64_bionic));
        free(entry_tmp);
        return &dirent_tmp;
    }

    return NULL;
}

int readdir_r_soloader(DIR * dirp, dirent64_bionic * entry,
                       dirent64_bionic ** result) {
    struct dirent dirent_tmp;
    struct dirent * pdirent_tmp;

    int ret = readdir_r(dirp, &dirent_tmp, &pdirent_tmp);

    if (ret == 0) {
        dirent64_bionic* entry_tmp = dirent_newlib_to_bionic(&dirent_tmp);
        memcpy(entry, entry_tmp, sizeof(dirent64_bionic));
        *result = (pdirent_tmp != NULL) ? entry : NULL;
        free(entry_tmp);
    }

    l_debug("readdir_r(%p, %p, %p): %i", dirp, entry, result, ret);
    return ret;
}

int closedir_soloader(DIR * dir) {
    int ret = closedir(dir);
    l_debug("closedir(%p): %i", dir, ret);
    return ret;
}

int fcntl_soloader(int fd, int cmd, ...) {
    l_warn("fcntl(%i, %i, ...): not implemented", fd, cmd);
    return 0;
}

int ioctl_soloader(int fd, int request, ...) {
    l_warn("ioctl(%i, %i, ...): not implemented", fd, request);
    return 0;
}

int fsync_soloader(int fd) {
    int ret = fsync(fd);
    l_debug("fsync(%i): %i", fd, ret);
    return ret;
}
