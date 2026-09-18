/**
 * @file audio.c
 * @brief Implementation of the android/media/AudioTrack -> sceAudioOut bridge.
 *        See audio.h for the full architectural picture and the exact JNI
 *        call sequence confirmed against this game's own decompiled .so.
 */

#include "audio.h"
#include "utils/logger.h"

#include <falso_jni/FalsoJNI_Impl.h>

#include <psp2/audioout.h>
#include <psp2/kernel/threadmgr.h>

#include <pthread.h>
#include <string.h>

// vox::DriverAndroid::_InitAT hardcodes 44100 Hz stereo 16-bit PCM
// (SetDriverSampleRate(0xac44) + getMinBufferSize(44100, CHANNEL_OUT_STEREO,
// ENCODING_PCM_16BIT)) -- not a guess, read directly from out_ghidra.c.
#define AT_SAMPLE_RATE  44100
#define AT_CHANNELS     2
#define AT_GRAIN_FRAMES 1024

// FIFO sized several grains deep so a burst of back-to-back write() calls
// from Vox's own dedicated audio thread (UpdateThreadedAT) never has to wait
// on the output thread mid-call under normal conditions.
#define AT_FIFO_BYTES (AT_GRAIN_FRAMES * AT_CHANNELS * (int)sizeof(short) * 8)

static pthread_mutex_t gLock = PTHREAD_MUTEX_INITIALIZER;
static unsigned char gFifo[AT_FIFO_BYTES];
static int gFifoLen = 0;

static volatile int gQuit = 0;
static int gPort = -1;
static SceUID gThread = -1;
static int gReady = 0;

static unsigned gWriteCalls = 0;
static unsigned long long gBytesTotal = 0;
static unsigned gTimeouts = 0;

/**
 * @brief Dedicated output thread: drains the AudioTrack FIFO into
 *        sceAudioOutOutput every grain, padding with silence if Vox hasn't
 *        produced enough data yet (never blocks the render/logic thread).
 */
static int audiotrack_output_thread(SceSize args, void *argp) {
    (void) args;
    (void) argp;
    static short buf[2][AT_GRAIN_FRAMES * AT_CHANNELS];
    int bufId = 0;
    const int wantBytes = AT_GRAIN_FRAMES * AT_CHANNELS * (int) sizeof(short);

    while (!gQuit) {
        short *dst = buf[bufId];

        pthread_mutex_lock(&gLock);
        int copyBytes = gFifoLen < wantBytes ? gFifoLen : wantBytes;
        copyBytes -= copyBytes % 4; // whole stereo 16-bit frames only
        if (copyBytes > 0) {
            memcpy(dst, gFifo, (size_t) copyBytes);
            int remaining = gFifoLen - copyBytes;
            if (remaining > 0) memmove(gFifo, gFifo + copyBytes, (size_t) remaining);
            gFifoLen = remaining;
        }
        pthread_mutex_unlock(&gLock);

        if (copyBytes < wantBytes) {
            memset((unsigned char *) dst + copyBytes, 0, (size_t) (wantBytes - copyBytes));
        }

        sceAudioOutOutput(gPort, dst);
        bufId ^= 1;
    }
    return 0;
}

void audio_init(void) {
    gFifoLen = 0;
    gQuit = 0;

    gPort = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, AT_GRAIN_FRAMES, AT_SAMPLE_RATE,
                                 SCE_AUDIO_OUT_MODE_STEREO);
    if (gPort < 0) {
        l_error("[audio] sceAudioOutOpenPort failed (0x%08X) -- game will stay silent", (unsigned) gPort);
        return;
    }

    // Pinned off the main render/logic thread's core (USER_0, see main.c):
    // this thread blocks on sceAudioOutOutput for the whole game session, so
    // it must not compete with the main thread for the same core.
    gThread = sceKernelCreateThread("audiotrack_out", audiotrack_output_thread, 0x10000100, 0x10000,
                                     0, SCE_KERNEL_CPU_MASK_USER_1, NULL);
    if (gThread < 0) {
        l_error("[audio] output thread creation failed (0x%08X) -- game will stay silent", (unsigned) gThread);
        sceAudioOutReleasePort(gPort);
        gPort = -1;
        return;
    }
    sceKernelStartThread(gThread, 0, NULL);
    gReady = 1;
    l_success("[audio] sceAudioOut ready (port=%d, %dHz stereo 16-bit) -- AudioTrack shim armed",
              gPort, AT_SAMPLE_RATE);
}

void audio_shutdown(void) {
    if (!gReady) return;
    gReady = 0;
    gQuit = 1;
    sceKernelWaitThreadEnd(gThread, NULL, NULL);
    sceKernelDeleteThread(gThread);
    gThread = -1;
    sceAudioOutReleasePort(gPort);
    gPort = -1;
}

jobject audiotrack_ctor(jmethodID id, va_list args) {
    (void) id;
    (void) args;
    // Never dereferenced by our own code -- only ever passed back into
    // CallNonvirtualIntMethod/CallVoidMethod as `this`, which our method
    // dispatch resolves purely by method name (see FalsoJNI.c), not by
    // inspecting the object. Just needs to be a stable non-NULL value.
    return (jobject) 0x41544b30; // "ATK0"
}

jint audiotrack_get_min_buffer_size(jmethodID id, va_list args) {
    (void) id;
    (void) args;
    // vox::DriverAndroid::_InitAT only uses this to size its own countdown
    // threshold (m_dataThreshold/m_updateTime), clamped internally to at most
    // 0x400 frames -- any plausible value works, this just needs to be a
    // multiple of the frame size (channels * sizeof(int16)).
    return AT_GRAIN_FRAMES * AT_CHANNELS * (jint) sizeof(short) * 4;
}

void audiotrack_noop_void(jmethodID id, va_list args) {
    (void) id;
    (void) args;
}

jint audiotrack_write(jmethodID id, va_list args) {
    (void) id;
    JavaDynArray *jda = (JavaDynArray *) va_arg(args, jobject);
    jint offset = va_arg(args, jint);
    jint sizeInBytes = va_arg(args, jint);

    if (!jda || !jda->array || sizeInBytes <= 0) return sizeInBytes;
    if (!gReady) return sizeInBytes;

    gWriteCalls++;
    if (gWriteCalls == 1) {
        l_success("[audio] first AudioTrack.write() call (sizeInBytes=%d) -- Vox output path is alive",
                  (int) sizeInBytes);
    }

    const unsigned char *src = (const unsigned char *) jda->array + offset;
    int remaining = (int) sizeInBytes;

    // Real Android AudioTrack.write() in MODE_STREAM blocks until there's
    // room -- Vox's own UpdateThreadedAT thread (see audio.h) expects that
    // backpressure. Mirrored here with a brief sleep instead of a spin, and a
    // bounded retry count so a genuinely stuck output thread can't hang Vox's
    // audio thread forever.
    int guard = 0;
    while (remaining > 0 && guard < 2000) {
        pthread_mutex_lock(&gLock);
        int space = AT_FIFO_BYTES - gFifoLen;
        int chunk = remaining < space ? remaining : space;
        if (chunk > 0) {
            memcpy(gFifo + gFifoLen, src, (size_t) chunk);
            gFifoLen += chunk;
            src += chunk;
            remaining -= chunk;
        }
        pthread_mutex_unlock(&gLock);
        if (remaining > 0) {
            sceKernelDelayThread(1000);
            guard++;
        }
    }

    gBytesTotal += (unsigned long long) (sizeInBytes - remaining);
    if (remaining > 0) {
        gTimeouts++;
        if (gTimeouts <= 5) {
            l_warn("[audio] AudioTrack.write() timed out waiting for FIFO room -- dropped %d/%d bytes (timeout #%u)",
                   remaining, (int) sizeInBytes, gTimeouts);
        }
    }
    if (gWriteCalls % 500 == 0) {
        l_info("[audio] %u AudioTrack.write() calls, %llu bytes total, %u timeouts so far",
               gWriteCalls, gBytesTotal, gTimeouts);
    }

    return sizeInBytes;
}

void audiotrack_release(jmethodID id, va_list args) {
    (void) id;
    (void) args;
    pthread_mutex_lock(&gLock);
    gFifoLen = 0;
    pthread_mutex_unlock(&gLock);
}
