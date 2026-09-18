/**
 * @file audio.h
 * @brief android/media/AudioTrack output shim backed by sceAudioOut.
 *
 * Sacred Odyssey (Gameloft "Glitch" engine) embeds its own native audio
 * middleware, vox::DriverAndroid (confirmed directly in this port's own
 * decompiled .so: decompiled/decompiled_so/libsacredodyssey_v106/out_ghidra.c,
 * vox::DriverAndroid::_InitAT/UpdateThreadedAT/DoCallbackAT). It decodes and
 * mixes every .wav/.vxn sound itself in native code and only ever touches
 * Java to push the final mixed buffer out via a single JNI surface:
 * android.media.AudioTrack, confirmed with these EXACT call sites in _InitAT:
 *   - DriverCallbackSourceInterface::SetDriverSampleRate(0xac44)  -> 44100 Hz
 *   - FindClass("android/media/AudioTrack")
 *   - GetMethodID(cls, "<init>", "(IIIIII)V")
 *   - GetMethodID(cls, "getMinBufferSize", "(III)I")   (static, but looked up
 *     the same way FalsoJNI's flat name table already handles)
 *   - GetMethodID(cls, "play"/"pause"/"stop"/"release", "()V")
 *   - GetMethodID(cls, "write", "([BII)I")
 *   - CallStaticIntMethod(cls, getMinBufferSize, 44100, 12, 2)  -- 12=CHANNEL_OUT_STEREO, 2=ENCODING_PCM_16BIT
 * DoCallbackAT then fills a jbyteArray directly in native code
 * (DriverCallbackInterface::_FillBuffer) and calls
 * CallNonvirtualIntMethod(..., mWrite, array, 0, frames*4) as the only actual
 * output. This matches port_progress.md's own confirmed diagnosis for this
 * exact game ("el driver VOX falla... AudioTrack driver could not
 * initialize... Audio real (VOX->sceAudio) es tarea futura separada") and the
 * identical architecture independently confirmed and shipped on the sibling
 * Gameloft "Glitch"-engine port Dungeon-Hunter-2-vita (same engine family,
 * same vox::DriverAndroid, same JNI call sequence byte-for-byte).
 *
 * Because of this, <init>/getMinBufferSize/play/pause/stop can stay as
 * harmless stubs -- VOX only uses getMinBufferSize's return value to size its
 * own internal update-buffer countdown, not to gate whether audio "works" --
 * and only write() (the real output sink) and release() need to be real.
 */

#ifndef SACREDODYSSEY_AUDIO_H
#define SACREDODYSSEY_AUDIO_H

#include <falso_jni/FalsoJNI.h>

#ifdef __cplusplus
extern "C" {
#endif

void audio_init(void);
void audio_shutdown(void);

jobject audiotrack_ctor(jmethodID id, va_list args);
jint audiotrack_get_min_buffer_size(jmethodID id, va_list args);
void audiotrack_noop_void(jmethodID id, va_list args);
jint audiotrack_write(jmethodID id, va_list args);
void audiotrack_release(jmethodID id, va_list args);

#ifdef __cplusplus
}
#endif

#endif // SACREDODYSSEY_AUDIO_H
