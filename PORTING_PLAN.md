# Plan de Port — Sacred Odyssey: Rise of Ayden (PS Vita)

> Documento vivo de desarrollo del port de Android a PS Vita (soloader). Actualizado con el análisis exhaustivo de símbolos, decompilación con Ghidra/jadx y bootstrap inicial completado.

---

## 0. Contexto y Motor Confirmado

- **Juego:** Sacred Odyssey: Rise of Ayden
- **Publisher / Desarrollador:** Gameloft
- **Paquete Java:** `com.gameloft.android.ANMP.GloftSOHP.ML`
- **APK original:** `Sacred-Odyssey-The-Raise-of-Ayden-v1.0.3-PowerVR.apk`
- **TITLEID asignado:** `PSVSOTROA`
- **Ruta de datos en Vita:** `ux0:data/sacredodyssey/` (original Android: `/sdcard/gameloft/games/GloftSOHP/`)

### Confirmación de Motor: Gameloft "Glitch" 3D Engine
El análisis estático de símbolos y constantes confirma sin ambigüedad que Sacred Odyssey utiliza el **motor Glitch** (propiedad de Gameloft), compartiendo linaje directo con:
- `Shadow Guardian` (`com.gameloft.android.ANMP.GloftSGHP.ML`)
- `Dungeon Hunter 2` (`com.gameloft.android.GAND.GloftD2SS`)
- `Asphalt 5`

**Características técnicas del motor:**
1. **Gráficos:** Pipeline programable OpenGL ES 2.0 con shaders GLSL (`a_position`, `a_texCoord`, `s_texture`). Formato de modelos 3D propietario `.bdae` (binario derivado de Collada).
2. **Archivos y empaquetado:** Contenedores ofuscados `.obfs` (`%s/data/%u.obfs`) y texturas comprimidas `.tga` / PVRTC.
3. **Ruta base del filesystem:** Variable global `m_gAppPath` configurada por la función `_Z8initPathv()`.
4. **Protección anticopia:** `ALicenseCheck` (`ALicenseCheck_ValidateLicense`, `ValidateNative`, `LoadRMS`).
5. **Audio:** Subsistema Gameloft `GLMediaPlayer` acoplado al middleware `Vox` (`VoxSetJavaVM`), reproduciendo pistas WAV / IMA-ADPCM.

---

## 1. Detección y Especificaciones de Arquitectura

- **ABI:** `armeabi` (ARMv6, softfp). Ejecutable nativamente en el Cortex-A9 de PS Vita sin emulación.
- **Render API:** OpenGL ES 2.0 vía `vitaGL` con ShaccCg / vitaShark para compilación en runtime de shaders GLSL.
- **Resolución:** Nativa PS Vita 960x544.

---

## 2. Binarios del Juego y Decompilación

- **Librería nativa:** `sacredodyssey_extract/lib/armeabi/libsacredodyssey.so` (6.8 MB).
- **Decompilación Java (jadx):** 100% completada en `decompiled/apk_jadx/`.
- **Decompilación Pseudo-C (Ghidra headless via devrvk/so-decompiler en Docker):** 100% completada en `decompiled/decompiled_so/libsacredodyssey_armeabi/` (`out_ghidra.c` ~19 MB, `out_ghidra.h` ~3 MB).
- **Importaciones dinámicas del .so:** 316 símbolos `UND` analizados.
  - 311 resueltos directamente por libc/newlib/vitaGL/pthreads de VitaSDK.
  - 5 símbolos requeridos agregados a `dynlib.c`:
    1. `_ZN6glitch4coreL17ROUNDING_ERROR_32E` (`1.0e-6f`)
    2. `_ZN6glitch4coreL17ROUNDING_ERROR_64E` (`1.0e-8`)
    3. `__aeabi_d2iz` (conversión double a int)
    4. `__dso_handle` (símbolo DSO estándar)
    5. `inet_addr` (resolución de sockets)

---

## 3. Interfaz JNI y Ciclo de Vida Nativo

### Símbolos nativos exportados por `libsacredodyssey.so`:
- `JNI_OnLoad(JavaVM *jvm, void *reserved)` -> Registra la JVM e inicializa `VoxSetJavaVM`.
- `Java_com_gameloft_android_ANMP_GloftSOHP_ML_GLUtils_Device_nativeInit(JNIEnv *env, jobject clazz)`
- `Java_com_gameloft_android_ANMP_GloftSOHP_ML_GLMediaPlayer_nativeInit(JNIEnv *env, jobject clazz)`
- `Java_com_gameloft_android_ANMP_GloftSOHP_ML_MediaPlaylist_nativeInit(JNIEnv *env, jobject clazz)`
- `Java_com_gameloft_android_ANMP_GloftSOHP_ML_GameRenderer_nativeInit(JNIEnv *env, jobject clazz, int manuf, int w, int h)`
- `Java_com_gameloft_android_ANMP_GloftSOHP_ML_Game_nativeInit(JNIEnv *env, jobject clazz)`
- `Java_com_gameloft_android_ANMP_GloftSOHP_ML_GameRenderer_nativeResize(JNIEnv *env, jobject clazz, int w, int h)`
- `Java_com_gameloft_android_ANMP_GloftSOHP_ML_GameRenderer_nativeRender(JNIEnv *env, jobject clazz)`
- `Java_com_gameloft_android_ANMP_GloftSOHP_ML_GameRenderer_nativeDone(JNIEnv *env, jobject clazz)`
- `Java_com_gameloft_android_ANMP_GloftSOHP_ML_Game_nativeOnKeyDown(JNIEnv *env, jobject clazz, int key)`
- `Java_com_gameloft_android_ANMP_GloftSOHP_ML_Game_nativeOnKeyUp(JNIEnv *env, jobject clazz, int key)`
- `Java_com_gameloft_android_ANMP_GloftSOHP_ML_GameGLSurfaceView_nativeOnTouch(JNIEnv *env, jobject clazz, int action, int x, int y, int pointerId)`
- `Java_com_gameloft_android_ANMP_GloftSOHP_ML_GameGLSurfaceView_nativePause(JNIEnv *env, jobject clazz)`
- `Java_com_gameloft_android_ANMP_GloftSOHP_ML_GameGLSurfaceView_nativeResume(JNIEnv *env, jobject clazz)`

### Callbacks hacia Java implementados en FalsoJNI (`source/java.c`):
- **Carga de recursos (`GLResLoader`):** `getResourceLength`, `getResourceBytes`, `getResourceFull`, `getRawResource` redirigidos al filesystem local de Vita (`DATA_PATH`).
- **Información del dispositivo (`Device`):** `getPhoneModel`, `getCarrier`, `getIMEI`, `getUserAgent`, `IsWifiEnable`, `isWifiEnabled`, `getManufacture`.
- **Ciclo de vida / UI:** `Exit`, `sendAppToBackground`, `Pause`, `openBrowser`, `enableWifi`, `launchGLLive`, `launchIGP`, `playVideo`, `ShowSpinner`, `DismissSpinner`, `SetReorientation`.
- **Audio / Media:** `isSoundLoaded`, `loadSound`, `playSound`, `stopSound`, `pauseSound`, `resumeSound`, `setVolume`, `loadMusic`, `playMusic`, `stopMusic`, `MediaPlaylist` methods.

---

## 4. Checklist de Fases

- [x] **Fase 0:** Prerrequisitos verificados (`jadx`, Docker `devrvk/so-decompiler`, VitaSDK 10.3.0 en `/Users/metalsyntax/vitasdk`).
- [x] **Fase 1:** Artefactos de entrada extraídos y localizados (`libsacredodyssey.so`, APK, datos).
- [x] **Fase 2:** Decompilación Java (jadx) y Pseudo-C (Ghidra headless en Docker completada: `out_ghidra.c` y `out_ghidra.h`).
- [x] **Fase 3:** Análisis del motor real completado con evidencia (Glitch engine, convención JNI estándar, lifecycle verificado).
- [x] **Fase 4:** Repositorio git inicializado con `.gitignore` anti-DMCA estricto.
- [x] **Fase 5:** Bootstrap del loader completado:
  - FalsoJNI integrado y enlazado.
  - `so_util` dinámico configurado.
  - 5 símbolos `UND` faltantes resueltos en `dynlib.c`.
  - Conflicto de símbolos EGL resuelto (usando EGL nativo de `vitaGL`).
  - Patches en `source/patch.c` implementados (`initPath_hook` redirigiendo `m_gAppPath` a `ux0:data/sacredodyssey/`, bypass de `ALicenseCheck`).
  - Compilación limpia con CMake y generación exitosa de `eboot.bin` y `sacredodyssey.vpk`.
- [x] **Fase 6:** Implementación de FalsoJNI en `source/java.c` con tabla de métodos, campos y soporte de recursos `GLResLoader`.
- [x] **Fase 7:** Implementación del ciclo de vida y bucle principal en `source/main.c` (JNI_OnLoad -> Device -> Renderer -> Media -> Game -> Resize -> Main Loop con Peek Touch y Buttons).
- [ ] **Fase 8:** Primer arranque en Vita3K / hardware real con captura de logs.
- [ ] **Fase 9:** Depuración gráfica (shaders GLSL en VitaGL, texturas PVRTC/TGA, buffers).
- [ ] **Fase 10:** Mapeo de controles táctiles / analógicos a los botones físicos de la Vita.
- [x] **Fase 11:** Implementación de audio nativo (bridge `android/media/AudioTrack` -> `sceAudioOut`
  para el middleware VOX del motor, confirmado como el único sink de audio real usado por
  `vox::DriverAndroid::DoCallbackAT` -- `source/audio.c`/`.h`, wireado en `source/java.c`; ver
  port_progress.md, sesión 2026-09-17). Pendiente de confirmación final en consola física.
- [ ] **Fase 12:** Empaquetado final y verificación en consola física.

---

## 5. Próximo Paso Inmediato

Copiar los archivos de datos (`GloftSOHP`) a `ux0:data/sacredodyssey/` en la consola física o Vita3K, transferir `eboot.bin` / `sacredodyssey.vpk` y ejecutar la primera prueba de arranque documentando el log en `port_progress.md`.
