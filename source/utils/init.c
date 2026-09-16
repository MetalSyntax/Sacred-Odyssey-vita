/*
 * Copyright (C) 2021      Andy Nguyen
 * Copyright (C) 2021-2022 Rinnegatamante
 * Copyright (C) 2022-2024 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "utils/init.h"

#include "utils/dialog.h"
#include "utils/embedded_shaders.h"
#include "utils/glutil.h"
#include "utils/logger.h"
#include "utils/utils.h"
#include "utils/settings.h"

#include <string.h>

#include <psp2/appmgr.h>
#include <psp2/apputil.h>
#include <psp2/kernel/clib.h>
#include <psp2/power.h>

#include <falso_jni/FalsoJNI.h>
#include <so_util/so_util.h>
#include <fios/fios.h>

#include <sys/stat.h>
#include <stdio.h>

static void ensure_shader_assets(void) {
    // Fuente primaria: shaders embebidos en el propio eboot (sin depender de
    // reinstalar el .vpk completo para que app0:/shaders/ exista en consola --
    // ese era el fallo silencioso que dejaba effects/ vacio y pintaba la vista
    // de carga de rosa). Escribe solo si falta o difiere en tamano.
    ensure_embedded_shaders_installed();

    // Secundario: si el .vpk instalado trae shaders mas nuevos en app0:/shaders/,
    // copiar los que todavia falten en effects/ (no pisa los ya instalados).
    static const char * const shader_names[] = {
        "ProfileCOMMON_emul_VS.glsl",
        "ProfileCOMMON_emul_FS.glsl",
        "UnlitOneTextureAndVertexColorVP.glsl",
        "UnlitTexturedFP.glsl",
        "UnlitTexturedBlendTextureAlphaFP.glsl",
        "UnlitMultiTexturedFP.glsl",
        "UnlitVertexColorVP.glsl",
        "UnlitVertexColorFP.glsl",
        "UnlitMaterialColorVP.glsl",
        "UnlitMaterialColorFP.glsl",
    };
    char target_dir[256];
    snprintf(target_dir, sizeof(target_dir), "%sGloftSOHP/data/3d/effects", DATA_PATH);
    file_mkpath(target_dir, 0777);

    for (size_t i = 0; i < sizeof(shader_names) / sizeof(shader_names[0]); i++) {
        char dst_path[256];
        snprintf(dst_path, sizeof(dst_path), "%s/%s", target_dir, shader_names[i]);
        if (!file_exists(dst_path)) {
            char src_path[256];
            snprintf(src_path, sizeof(src_path), "app0:/shaders/%s", shader_names[i]);
            FILE *src = fopen(src_path, "rb");
            if (src) {
                FILE *dst = fopen(dst_path, "wb");
                if (dst) {
                    char buf[1024];
                    size_t n;
                    while ((n = fread(buf, 1, sizeof(buf), src)) > 0) {
                        fwrite(buf, 1, n, dst);
                    }
                    fclose(dst);
                    l_info("Installed shader %s to %s", shader_names[i], dst_path);
                }
                fclose(src);
            }
        }
    }
}

// Base address for the Android .so to be loaded at
#define LOAD_ADDRESS 0x98000000

extern so_module so_mod;

void soloader_init_all() {
    ensure_shader_assets();
	// Launch `app0:configurator.bin` on `-config` init param
    sceAppUtilInit(&(SceAppUtilInitParam){}, &(SceAppUtilBootParam){});
    SceAppUtilAppEventParam eventParam;
    sceClibMemset(&eventParam, 0, sizeof(SceAppUtilAppEventParam));
    sceAppUtilReceiveAppEvent(&eventParam);
    if (eventParam.type == 0x05) {
        char buffer[2048];
        sceAppUtilAppEventParseLiveArea(&eventParam, buffer);
        if (strstr(buffer, "-config"))
            sceAppMgrLoadExec("app0:/configurator.bin", NULL, NULL);
    }

    // Set default overclock values
    scePowerSetArmClockFrequency(444);
    scePowerSetBusClockFrequency(222);
    scePowerSetGpuClockFrequency(222);
    scePowerSetGpuXbarClockFrequency(166);

#ifdef USE_SCELIBC_IO
    if (fios_init(DATA_PATH) == 0)
        l_success("FIOS initialized.");
#endif

    if (!module_loaded("kubridge")) {
        l_fatal("kubridge is not loaded.");
        fatal_error("Error: kubridge.skprx is not installed.");
    }
    l_success("kubridge check passed.");

    if (!file_exists(SO_PATH)) {
        fatal_error("Looks like you haven't installed the data files for this "
                    "port, or they are in an incorrect location. Please make "
                    "sure that you have %s file exactly at that path.", SO_PATH);
    }

    if (so_file_load(&so_mod, SO_PATH, LOAD_ADDRESS) < 0) {
        l_fatal("SO could not be loaded.");
        fatal_error("Error: could not load %s.", SO_PATH);
    }

    settings_load();
    l_success("Settings loaded.");

    so_relocate(&so_mod);
    l_success("SO relocated.");

    resolve_imports(&so_mod);
    l_success("SO imports resolved.");

    so_patch();
    l_success("SO patched.");

    so_flush_caches(&so_mod);
    l_success("SO caches flushed.");

    so_initialize(&so_mod);
    l_success("SO initialized.");

    gl_preload();
    l_success("OpenGL preloaded.");

    jni_init();
    l_success("FalsoJNI initialized.");
}
