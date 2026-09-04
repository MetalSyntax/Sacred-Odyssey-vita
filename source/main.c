#include "utils/init.h"
#include "utils/glutil.h"
#include "utils/logger.h"
#include "utils/dialog.h"

#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <psp2/power.h>
#include <psp2/rtc.h>

#include <falso_jni/FalsoJNI.h>
#include <so_util/so_util.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

void game_log(const char *fmt, ...) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    l_info("%s", buf);
}

int _newlib_heap_size_user = 256 * 1024 * 1024;
unsigned int sceUserMainThreadStackSize = 1 * 1024 * 1024;

#ifdef USE_SCELIBC_IO
int sceLibcHeapSize = 32 * 1024 * 1024;
#endif

so_module so_mod;

#define SCREEN_W 960
#define SCREEN_H 544

// Native function pointer types
typedef void (*so_void_fn)(JNIEnv *, jobject);
typedef void (*so_renderer_init_fn)(JNIEnv *, jobject, jint, jint, jint);
typedef void (*so_resize_fn)(JNIEnv *, jobject, jint, jint);
typedef void (*so_touch_fn)(JNIEnv *, jobject, jint, jint, jint, jint);
typedef void (*so_key_fn)(JNIEnv *, jobject, jint);
typedef int  (*so_can_interrupt_fn)(JNIEnv *, jobject);

static so_void_fn          nativeDeviceInit = NULL;
static so_void_fn          nativeGLMediaPlayerInit = NULL;
static so_void_fn          nativeMediaPlaylistInit = NULL;
static so_renderer_init_fn nativeGameRendererInit = NULL;
static so_void_fn          nativeGameInit = NULL;
static so_resize_fn        nativeGameRendererResize = NULL;
static so_void_fn          nativeGameRendererRender = NULL;
static so_void_fn          nativeGameRendererDone = NULL;
static so_void_fn          nativeGameGLSurfaceViewPause = NULL;
static so_void_fn          nativeGameGLSurfaceViewResume = NULL;
static so_touch_fn         nativeGameGLSurfaceViewOnTouch = NULL;
static so_key_fn           nativeOnKeyDown = NULL;
static so_key_fn           nativeOnKeyUp = NULL;
static so_can_interrupt_fn nativeCanInterrupt = NULL;

static void *resolve_sym_or_die(const char *name) {
    void *sym = (void *)so_symbol(&so_mod, name);
    if (!sym) {
        fatal_error("Required symbol '%s' not found in libsacredodyssey.so!", name);
    }
    l_info("Resolved %s -> %p", name, sym);
    return sym;
}

static void resolve_entrypoints(void) {
    nativeDeviceInit = (so_void_fn)resolve_sym_or_die(
        "Java_com_gameloft_android_ANMP_GloftSOHP_ML_GLUtils_Device_nativeInit");
    nativeGLMediaPlayerInit = (so_void_fn)resolve_sym_or_die(
        "Java_com_gameloft_android_ANMP_GloftSOHP_ML_GLMediaPlayer_nativeInit");
    nativeMediaPlaylistInit = (so_void_fn)resolve_sym_or_die(
        "Java_com_gameloft_android_ANMP_GloftSOHP_ML_MediaPlaylist_nativeInit");
    nativeGameRendererInit = (so_renderer_init_fn)resolve_sym_or_die(
        "Java_com_gameloft_android_ANMP_GloftSOHP_ML_GameRenderer_nativeInit");
    nativeGameInit = (so_void_fn)resolve_sym_or_die(
        "Java_com_gameloft_android_ANMP_GloftSOHP_ML_Game_nativeInit");
    nativeGameRendererResize = (so_resize_fn)resolve_sym_or_die(
        "Java_com_gameloft_android_ANMP_GloftSOHP_ML_GameRenderer_nativeResize");
    nativeGameRendererRender = (so_void_fn)resolve_sym_or_die(
        "Java_com_gameloft_android_ANMP_GloftSOHP_ML_GameRenderer_nativeRender");
    nativeGameRendererDone = (so_void_fn)resolve_sym_or_die(
        "Java_com_gameloft_android_ANMP_GloftSOHP_ML_GameRenderer_nativeDone");
    nativeGameGLSurfaceViewPause = (so_void_fn)resolve_sym_or_die(
        "Java_com_gameloft_android_ANMP_GloftSOHP_ML_GameGLSurfaceView_nativePause");
    nativeGameGLSurfaceViewResume = (so_void_fn)resolve_sym_or_die(
        "Java_com_gameloft_android_ANMP_GloftSOHP_ML_GameGLSurfaceView_nativeResume");
    nativeGameGLSurfaceViewOnTouch = (so_touch_fn)resolve_sym_or_die(
        "Java_com_gameloft_android_ANMP_GloftSOHP_ML_GameGLSurfaceView_nativeOnTouch");
    nativeOnKeyDown = (so_key_fn)resolve_sym_or_die(
        "Java_com_gameloft_android_ANMP_GloftSOHP_ML_Game_nativeOnKeyDown");
    nativeOnKeyUp = (so_key_fn)resolve_sym_or_die(
        "Java_com_gameloft_android_ANMP_GloftSOHP_ML_Game_nativeOnKeyUp");
    nativeCanInterrupt = (so_can_interrupt_fn)resolve_sym_or_die(
        "Java_com_gameloft_android_ANMP_GloftSOHP_ML_Game_nativeCanInterrupt");
}

int main(void) {
    // Maximize hardware clocks
    scePowerSetArmClockFrequency(444);
    scePowerSetBusClockFrequency(222);
    scePowerSetGpuClockFrequency(222);
    scePowerSetGpuXbarClockFrequency(166);

    // Dedicate main thread to core 0
    sceKernelChangeThreadCpuAffinityMask(sceKernelGetThreadId(), SCE_KERNEL_CPU_MASK_USER_0);

    l_info("Starting Sacred Odyssey loader initialization...");
    soloader_init_all();

    int (*JNI_OnLoad)(void *jvm) = (void *)so_symbol(&so_mod, "JNI_OnLoad");
    if (!JNI_OnLoad) {
        fatal_error("JNI_OnLoad not found in libsacredodyssey.so!");
    }
    l_info("Calling JNI_OnLoad...");
    JNI_OnLoad(&jvm);

    resolve_entrypoints();

    gl_init();
    l_info("vitaGL initialized successfully.");

    // Sequence corresponding to Game.onCreate & GameRenderer.onDrawFrame (first frame)
    l_info("Initializing Device...");
    nativeDeviceInit(&jni, NULL);

    l_info("Initializing GameRenderer (manufacturer=0, %dx%d)...", SCREEN_W, SCREEN_H);
    nativeGameRendererInit(&jni, NULL, 0, SCREEN_W, SCREEN_H);

    l_info("Initializing GLMediaPlayer...");
    nativeGLMediaPlayerInit(&jni, NULL);

    l_info("Initializing MediaPlaylist...");
    nativeMediaPlaylistInit(&jni, NULL);

    l_info("Initializing Game...");
    nativeGameInit(&jni, NULL);

    l_info("Calling GameRenderer.nativeResize(%dx%d)...", SCREEN_W, SCREEN_H);
    nativeGameRendererResize(&jni, NULL, SCREEN_W, SCREEN_H);

    // Enable touch and controller sampling
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);

    SceCtrlData pad;
    SceTouchData touch;
    SceTouchData touch_old;
    memset(&touch, 0, sizeof(touch));
    memset(&touch_old, 0, sizeof(touch_old));

    uint32_t old_buttons = 0;
    uint32_t current_buttons = 0;
    // Ensure initial video/pause flags allow normal rendering
    volatile uint8_t *v_isVideoFinish = (volatile uint8_t *)so_symbol(&so_mod, "isVideoFinish");
    if (v_isVideoFinish) *v_isVideoFinish = 1;
    volatile uint8_t *v_s_bReturnFromVideo = (volatile uint8_t *)so_symbol(&so_mod, "s_bReturnFromVideo");
    if (v_s_bReturnFromVideo) *v_s_bReturnFromVideo = 0;
    volatile uint8_t *v_m_bIsPlayMovie = (volatile uint8_t *)so_symbol(&so_mod, "m_bIsPlayMovie");
    if (v_m_bIsPlayMovie) *v_m_bIsPlayMovie = 0;

    volatile uint32_t *v_g_appAlive = (volatile uint32_t *)so_symbol(&so_mod, "g_appAlive");
    volatile uint32_t *v_g_appPaused = (volatile uint32_t *)so_symbol(&so_mod, "g_appPaused");

    l_info("Entering main render loop...");
    int frame_count = 0;
    SceRtcTick last_fps_tick;
    sceRtcGetCurrentTick(&last_fps_tick);

    while (1) {
        sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DEFAULT);

        // Controller buttons
        if (sceCtrlPeekBufferPositive(0, &pad, 1) > 0) {
            old_buttons = current_buttons;
            current_buttons = pad.buttons;
            uint32_t pressed = current_buttons & ~old_buttons;
            uint32_t released = ~current_buttons & old_buttons;

            // START or CIRCLE -> KEYCODE_BACK (4)
            if (pressed & (SCE_CTRL_START | SCE_CTRL_CIRCLE)) {
                nativeOnKeyDown(&jni, NULL, 4);
            }
            if (released & (SCE_CTRL_START | SCE_CTRL_CIRCLE)) {
                nativeOnKeyUp(&jni, NULL, 4);
            }

            // SELECT -> KEYCODE_MENU (82)
            if (pressed & SCE_CTRL_SELECT) {
                nativeOnKeyDown(&jni, NULL, 82);
            }
            if (released & SCE_CTRL_SELECT) {
                nativeOnKeyUp(&jni, NULL, 82);
            }
        }

        // Touch handling (front panel)
        memcpy(&touch_old, &touch, sizeof(touch_old));
        if (sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1) > 0) {
            for (int i = 0; i < touch.reportNum; i++) {
                int x = (int)(touch.report[i].x * SCREEN_W / 1920.0f);
                int y = (int)(touch.report[i].y * SCREEN_H / 1088.0f);
                int id = touch.report[i].id;

                int found = 0;
                for (int j = 0; j < touch_old.reportNum; j++) {
                    if (touch_old.report[j].id == id) {
                        found = 1;
                        break;
                    }
                }

                if (!found) {
                    nativeGameGLSurfaceViewOnTouch(&jni, NULL, 1, x, y, id);
                } else {
                    nativeGameGLSurfaceViewOnTouch(&jni, NULL, 2, x, y, id);
                }
            }

            for (int j = 0; j < touch_old.reportNum; j++) {
                int old_id = touch_old.report[j].id;
                int still_present = 0;
                for (int i = 0; i < touch.reportNum; i++) {
                    if (touch.report[i].id == old_id) {
                        still_present = 1;
                        break;
                    }
                }
                if (!still_present) {
                    int old_x = (int)(touch_old.report[j].x * SCREEN_W / 1920.0f);
                    int old_y = (int)(touch_old.report[j].y * SCREEN_H / 1088.0f);
                    nativeGameGLSurfaceViewOnTouch(&jni, NULL, 0, old_x, old_y, old_id);
                }
            }
        }

        // Render frame
        nativeGameRendererRender(&jni, NULL);
        gl_swap();

        frame_count++;
        if (frame_count % 60 == 0) {
            SceRtcTick now_tick;
            sceRtcGetCurrentTick(&now_tick);
            uint64_t elapsed_us = now_tick.tick - last_fps_tick.tick;
            float fps = (elapsed_us > 0) ? (60.0f * 1000000.0f / (float)elapsed_us) : 0.0f;
            last_fps_tick = now_tick;

            GLenum err = glGetError();
            l_info("[render_diag] frame=%d fps=%.1f glGetError=0x%04x (alive=%u, paused=%u, movie=%u)",
                   frame_count, fps, (unsigned int)err,
                   v_g_appAlive ? *v_g_appAlive : 0,
                   v_g_appPaused ? *v_g_appPaused : 0,
                   v_m_bIsPlayMovie ? *v_m_bIsPlayMovie : 0);
        }
    }

    if (nativeGameRendererDone) {
        nativeGameRendererDone(&jni, NULL);
    }

    sceKernelExitDeleteThread(0);
    return 0;
}
