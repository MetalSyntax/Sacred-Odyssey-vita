#ifndef __CONTROLS_H__
#define __CONTROLS_H__

#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <stdint.h>
#include <stdbool.h>

#include <falso_jni/FalsoJNI.h>

#define SCREEN_W 800
#define SCREEN_H 480

typedef void (*so_touch_fn)(JNIEnv *, jobject, jint, jint, jint, jint);
typedef void (*so_key_fn)(JNIEnv *, jobject, jint);

/**
 * Initialize control hooks and symbol resolutions.
 * Hooks HudMovePad and CameraRotatePad for analog stick and D-pad support,
 * and sets up dynamic widget tracking.
 */
void controls_init(so_touch_fn touch_fn, so_key_fn key_down_fn, so_key_fn key_up_fn, JNIEnv *jni_env);

/**
 * Update controls each frame.
 * Reads controller input and front touch panel, maps physical buttons to
 * dynamic on-screen virtual widgets, and manages touch slots safely without heap corruption.
 */
void controls_update(void);

#endif // __CONTROLS_H__
