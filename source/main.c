#include "utils/init.h"
#include "utils/glutil.h"
#include "utils/logger.h"
#include "utils/dialog.h"
#include "controls.h"

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

// Resolución reportada al engine (el layout de menús/UI del motor necesita
// EXACTAMENTE 800x480, confirmado en hardware que 960x544 rompe el layout).
// El engine dibuja DIRECTO sobre el framebuffer real -- ningún FBO
// intermedio -- y glViewport_soloader/glScissor_soloader (glutil.c) reescalan
// cada rect de su espacio 800x480 a los 960x544 reales antes de tocar la GPU.
// El táctil mapea del panel (1920x1088) a este mismo espacio 800x480.
#define SCREEN_W 800
#define SCREEN_H 480

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
    l_info("eboot build stamp: %s %s", __DATE__, __TIME__);
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

    // Initialize physical-to-virtual controls mapping, analog stick hooks, and widget tracking
    controls_init(nativeGameGLSurfaceViewOnTouch, nativeOnKeyDown, nativeOnKeyUp, &jni);

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
    // Tracks whether fps has recovered from the real, one-time boot stall
    // (shader translation + first scene load: minutes of near-0 fps, confirmed
    // in port_progress.md) -- pure [render_diag] telemetry now (the debug-only
    // loading-bar overlay that used to key off this was removed: the game has
    // its own loading screen). Latched permanently once fast, so a later,
    // unrelated one-frame hiccup can't flip it back.
    int loading_done = 0;

    while (1) {
        sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DEFAULT);

        // Update physical controls, analog sticks, virtual buttons, and touch slots
        controls_update();

        // Render frame, then swap.
        nativeGameRendererRender(&jni, NULL);
        gl_swap();

        frame_count++;
        if (frame_count % 60 == 0) {
            SceRtcTick now_tick;
            sceRtcGetCurrentTick(&now_tick);
            uint64_t elapsed_us = now_tick.tick - last_fps_tick.tick;
            float fps = (elapsed_us > 0) ? (60.0f * 1000000.0f / (float)elapsed_us) : 0.0f;
            last_fps_tick = now_tick;

            // A window this slow can only be the one-time boot stall (shader
            // translation / first scene load) -- confirmed dropping to
            // fps=0.4 for a whole 60-frame window in log_20260910_013817.txt.
            // Once a full window clears comfortably above that, loading is
            // over; latch permanently so a later, unrelated one-frame hiccup
            // can't flip it back.
            if (!loading_done && fps > 30.0f) {
                loading_done = 1;
                l_info("[render_diag] boot stall cleared (fps=%.1f)", fps);
            }

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
