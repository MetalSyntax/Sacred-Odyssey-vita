/*
 * Copyright (C) 2023 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

/**
 * @file  patch.c
 * @brief Patching some of the .so internal functions or bridging them to native
 *        for better compatibility.
 */

#include <kubridge.h>
#include <so_util/so_util.h>
#include "utils/logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>

extern so_module so_mod;

static char **m_gAppPath_ptr = NULL;

static void initPath_hook(void) {
    if (m_gAppPath_ptr) {
        if (!*m_gAppPath_ptr) {
            *m_gAppPath_ptr = (char *)malloc(512);
        }
        // El motor espera la misma estructura que en Android
        // (/sdcard/gameloft/games/GloftSOHP/), donde GloftSOHP/ es la carpeta
        // que contiene data/*.obfs, intro/logo.m4v, etc.
        snprintf(*m_gAppPath_ptr, 512, "%sGloftSOHP/", DATA_PATH);
        chdir(*m_gAppPath_ptr);
        l_info("initPath hook: m_gAppPath set to %s", *m_gAppPath_ptr);
    }
}

static void license_validate_stub(bool param_1) {
    (void)param_1;
    l_debug("ALicenseCheck_ValidateLicense bypassed");
}

static int license_validate_native_stub(void) {
    l_debug("ALicenseCheck::ValidateNative bypassed -> returning 1");
    return 1;
}

static void license_init_stub(void *env, void *clazz) {
    (void)env;
    (void)clazz;
    l_info("ALicenseCheck_InitLicense bypassed");
}

static void license_load_config_stub(void) {
    l_info("ALicenseCheck::LoadConfig bypassed");
}

void so_patch(void) {
    m_gAppPath_ptr = (char **)so_symbol(&so_mod, "m_gAppPath");

    uintptr_t initPath_sym = so_symbol(&so_mod, "_Z8initPathv");
    if (initPath_sym) {
        hook_addr(initPath_sym, (uintptr_t)&initPath_hook);
        l_info("Hooked _Z8initPathv successfully");
    } else {
        l_warn("Could not find _Z8initPathv to hook");
    }

    uintptr_t license_sym = so_symbol(&so_mod, "ALicenseCheck_ValidateLicense");
    if (license_sym) {
        hook_addr(license_sym, (uintptr_t)&license_validate_stub);
        l_info("Hooked ALicenseCheck_ValidateLicense successfully");
    }

    uintptr_t license_native_sym = so_symbol(&so_mod, "_ZN13ALicenseCheck14ValidateNativeEv");
    if (license_native_sym) {
        hook_addr(license_native_sym, (uintptr_t)&license_validate_native_stub);
        l_info("Hooked _ZN13ALicenseCheck14ValidateNativeEv successfully");
    }

    uintptr_t license_init_sym = so_symbol(&so_mod, "ALicenseCheck_InitLicense");
    if (license_init_sym) {
        hook_addr(license_init_sym, (uintptr_t)&license_init_stub);
        l_info("Hooked ALicenseCheck_InitLicense successfully");
    }

    uintptr_t license_init_cpp = so_symbol(&so_mod, "_ZN13ALicenseCheck4InitEP7_JNIEnvP7_jclass");
    if (license_init_cpp) {
        hook_addr(license_init_cpp, (uintptr_t)&license_init_stub);
        l_info("Hooked _ZN13ALicenseCheck4InitEP7_JNIEnvP7_jclass successfully");
    }

    uintptr_t license_load_cfg = so_symbol(&so_mod, "_ZN13ALicenseCheck10LoadConfigEv");
    if (license_load_cfg) {
        hook_addr(license_load_cfg, (uintptr_t)&license_load_config_stub);
        l_info("Hooked _ZN13ALicenseCheck10LoadConfigEv successfully");
    }

    uintptr_t license_val_srv = so_symbol(&so_mod, "_ZN13ALicenseCheck14ValidateServerEb");
    if (license_val_srv) {
        hook_addr(license_val_srv, (uintptr_t)&license_validate_stub);
        l_info("Hooked _ZN13ALicenseCheck14ValidateServerEb successfully");
    }

    // Fix Data abort in ShowItemEffectProxy::ShowItemEffectProxy() (PC: 0x001c2bbc).
    // When getParameterID(6, 0) returns 0xffff (parameter not found), the original code did:
    //   1c2bac: cmp r2, r1        ; r2 = num_params, r1 = 0xffff
    //   1c2bb0: movls r3, #0      ; r3 = 0
    //   1c2bbc: ldrb r3, [r3, #6] ; CRASH: Data abort dereferencing NULL + 6!
    // We patch 0x001c2bb0 from "movls r3, #0" (0x93a03000) to "bls 0x1c2bec" (0x9a00000d)
    // which safely jumps to 0x001c2bec (skipping setParameter) when the parameter is missing.
    uint32_t patch_show_item = 0x9a00000d;
    kuKernelCpuUnrestrictedMemcpy((void *)(so_mod.text_base + 0x001c2bb0), &patch_show_item, sizeof(uint32_t));
    l_info("Patched ShowItemEffectProxy missing parameter crash (0x001c2bb0 -> bls 0x1c2bec)");

    // Fix Data abort in Application::Update() (PC: 0x001bdf84).
    // Original code in Application::Update():
    //   1bdf80: ldr r3, [r3]          ; r3 = Gameplay::s_instance (NULL in menus / startup!)
    //   1bdf84: ldr r3, [r3, #32]     ; CRASH: Data abort dereferencing NULL + 32!
    //   1bdf88: cmp r3, #0
    //   1bdf8c: beq 1bdfac
    // We insert a trampoline into the unused ALicenseCheck::CallJNIFuncChar region (0x003dd010)
    // that safely checks:
    //   if (!Gameplay::s_instance || !Gameplay::s_instance->m_player) goto 1bdfac;
    // and checks the timeScale if valid before returning to 1be018 or 1bdfac.
    // In 0x001bdf80, we replace "ldr r3, [r3]" with "b 0x003dd010" (0xea087c22).
    static const uint32_t patch_update_trampoline[] = {
        0xe5933000, // ldr r3, [r3]
        0xe3530000, // cmp r3, #0
        0x0af783e3, // beq 0x1bdfac
        0xe5933020, // ldr r3, [r3, #32]
        0xe3530000, // cmp r3, #0
        0x0af783e0, // beq 0x1bdfac
        0xeeb77a00, // vmov.f32 s14, #112 (1.0f)
        0xe30120fc, // movw r2, #4348
        0xe0822003, // add r2, r2, r3
        0xedd27a00, // vldr s15, [r2]
        0xeef47a47, // vcmp.f32 s15, s14
        0xeef1fa10, // vmrs APSR_nzcv, fpscr
        0x1af783f4, // bne 0x1be018
        0xeaf783d8, // b 0x1bdfac
    };
    kuKernelCpuUnrestrictedMemcpy((void *)(so_mod.text_base + 0x003dd010), patch_update_trampoline, sizeof(patch_update_trampoline));

    uint32_t patch_app_update = 0xea087c22; // b 0x003dd010
    kuKernelCpuUnrestrictedMemcpy((void *)(so_mod.text_base + 0x001bdf80), &patch_app_update, sizeof(uint32_t));
    l_info("Patched Application::Update Gameplay::s_instance NULL check (0x001bdf80 -> b 0x003dd010)");

    // Fix Data abort in Application::_Draw(int) (PC: 0x001bd104).
    // Original code:
    //   1bd100: ldr r3, [r2]          ; r3 = Gameplay::s_instance (NULL in menus / startup!)
    //   1bd104: ldrb sl, [r3, #36]    ; CRASH: Data abort dereferencing NULL + 36!
    //   1bd108: cmp sl, #0
    //   1bd10c: beq 1bd210
    // We insert a trampoline into unused region (0x003dd050):
    //   mov sl, #0
    //   cmp r3, #0
    //   ldrneb sl, [r3, #36]
    //   cmp sl, #0
    //   beq 0x1bd210
    //   b 0x1bd110
    // In 0x001bd104, we replace "ldrb sl, [r3, #36]" with "b 0x003dd050" (0xea087fd1).
    static const uint32_t patch_draw_trampoline[] = {
        0xe3a0a000, // mov sl, #0
        0xe3530000, // cmp r3, #0
        0x15d3a024, // ldrbne sl, [r3, #36]
        0xe35a0000, // cmp sl, #0
        0x0af7806a, // beq 0x1bd210
        0xeaf78029, // b 0x1bd110
    };
    kuKernelCpuUnrestrictedMemcpy((void *)(so_mod.text_base + 0x003dd050), patch_draw_trampoline, sizeof(patch_draw_trampoline));

    uint32_t patch_app_draw = 0xea087fd1; // b 0x003dd050
    kuKernelCpuUnrestrictedMemcpy((void *)(so_mod.text_base + 0x001bd104), &patch_app_draw, sizeof(uint32_t));
    l_info("Patched Application::_Draw Gameplay::s_instance NULL check (0x001bd104 -> b 0x003dd050)");

    // Fix prospective Data abort in Application::Draw2D() (0x001bdc14 - 0x001bdc20).
    // Original code:
    //   1bdc14: ldr r3, [r3]          ; r3 = Gameplay::s_instance
    //   1bdc18: ldr r3, [r3, #32]     ; CRASH: if Gameplay::s_instance is NULL!
    //   1bdc1c: cmp r3, #0
    //   1bdc20: beq 1bdc70
    // We insert a trampoline into unused region (0x003dd070):
    //   ldr r3, [r3]
    //   cmp r3, #0
    //   beq 0x1bdc70
    //   ldr r3, [r3, #32]
    //   cmp r3, #0
    //   beq 0x1bdc70
    //   b 0x1bdc24
    // In 0x001bdc14, we replace "ldr r3, [r3]" with "b 0x003dd070" (0xea087d15).
    static const uint32_t patch_draw2d_trampoline[] = {
        0xe5933000, // ldr r3, [r3]
        0xe3530000, // cmp r3, #0
        0x0af782fc, // beq 0x1bdc70
        0xe5933020, // ldr r3, [r3, #32]
        0xe3530000, // cmp r3, #0
        0x0af782f9, // beq 0x1bdc70
        0xeaf782e5, // b 0x1bdc24
    };
    kuKernelCpuUnrestrictedMemcpy((void *)(so_mod.text_base + 0x003dd070), patch_draw2d_trampoline, sizeof(patch_draw2d_trampoline));

    uint32_t patch_app_draw2d = 0xea087d15; // b 0x003dd070
    kuKernelCpuUnrestrictedMemcpy((void *)(so_mod.text_base + 0x001bdc14), &patch_app_draw2d, sizeof(uint32_t));
    l_info("Patched Application::Draw2D Gameplay::s_instance NULL check (0x001bdc14 -> b 0x003dd070)");
}
