# Registro de Progreso — Sacred Odyssey (PS Vita)

## Fase 0: Prerrequisitos y Entorno (Completada — 2026-09-03)
- `jadx` verificado en `/opt/homebrew/bin/jadx`.
- Docker verificado con imagen `devrvk/so-decompiler:latest`.
- VitaSDK verificado en `/Users/metalsyntax/vitasdk` (GCC 10.3.0).

## Fase 1: Preparación y Artefactos (Completada — 2026-09-01)
- APK `Sacred-Odyssey-The-Raise-of-Ayden-v1.0.3-PowerVR.apk` analizado y extraído.
- Librería nativa identificada: `sacredodyssey_extract/lib/armeabi/libsacredodyssey.so` (6.8 MB).
- ABI: `armeabi` (ARMv6, soft-float). Totalmente compatible con la CPU Cortex-A9 de PS Vita.
- Renderizador: GLES 2.0 (shaders GLSL).

## Fase 2: Decompilación (Completada — 2026-09-03)
- **Java (jadx):** Decompilado en `decompiled/apk_jadx/` (547 líneas en `Game.java`, `GameRenderer.java`, `GameGLSurfaceView.java`, `GLResLoader.java`).
- **Pseudo-C (Ghidra headless en Docker):** Ejecutado con `devrvk/so-decompiler` (`--platform linux/amd64`).
  - Salida: `decompiled/decompiled_so/libsacredodyssey_armeabi/out_ghidra.c` (19.1 MB) y `out_ghidra.h` (2.9 MB).

## Fase 3: Análisis del Motor Real (Completada — 2026-09-03)
- **Motor confirmado:** Gameloft "Glitch" 3D Engine (mismo motor de *Shadow Guardian*, *Dungeon Hunter 2* y *Asphalt 5*).
- **Ciclo de vida nativo verificado:**
  1. `JNI_OnLoad` -> Inicializa el middleware de audio Vox (`VoxSetJavaVM`).
  2. `Device.nativeInit()`
  3. `GameRenderer.nativeInit(manuf=0, width=960, height=544)`
  4. `GLMediaPlayer.nativeInit()`
  5. `MediaPlaylist.nativeInit()`
  6. `Game.nativeInit()`
  7. `GameRenderer.nativeResize(960, 544)`
  8. En cada frame: `GameRenderer.nativeRender()` seguido de `gl_swap()`.
- **Imports analizados:** 316 símbolos UND en el `.so`. Solo 5 faltaban en el loader base:
  - `_ZN6glitch4coreL17ROUNDING_ERROR_32E` (constante float `1.0e-6f`)
  - `_ZN6glitch4coreL17ROUNDING_ERROR_64E` (constante double `1.0e-8`)
  - `__aeabi_d2iz` (conversión en libgcc)
  - `__dso_handle` (declarado extern)
  - `inet_addr` (incluido desde `<arpa/inet.h>`)
- **Puntos críticos de parcheo:**
  - `_Z8initPathv()` y `m_gAppPath`: Redirigido a `DATA_PATH` (`ux0:data/sacredodyssey/`).
  - `ALicenseCheck_ValidateLicense` y `ValidateNative`: Bypasseados en `source/patch.c`.

## Fase 4: Git y Reglas Anti-DMCA (Completada — 2026-09-03)
- `.gitignore` configurado estrictamente para no rastrear ningún archivo propietario:
  - `*.apk`, `*.zip`, carpetas extraídas (`sacredodyssey_extract/`, `GloftSOHP/`).
  - Resultados decompilados (`/decompiled/`).
  - Binarios y bibliotecas del juego (`lib/*.so`, `sacredodyssey_extract/lib/`).
  - Artefactos de build (`*.elf`, `*.self`, `*.vpk`, `*.bin`).

## Fase 5: Bootstrap del Loader y Primer Build (Completada — 2026-09-03)
- `FalsoJNI` incorporado e integrado en `lib/falso_jni/`.
- Resuelto conflicto de símbolos EGL duplicados removiendo `source/reimpl/egl.c` de `CMakeLists.txt` (vitaGL provee EGL nativo).
- Implementada función de log `game_log()` requerida por `FalsoJNI_Logger.c`.
- Agregadas las 5 resoluciones de símbolos faltantes en `source/dynlib.c`.
- Resuelto el problema de espacios en la ruta (`vita-pack-vpk`) utilizando symlink en `/tmp/sacredodyssey-src` y build en `/tmp/sacredodyssey-build`.
- **Primer build 100% exitoso:** Generados `eboot.bin` (601 KB) y `sacredodyssey.vpk` (683 KB).

## Fase 6: Implementación de FalsoJNI (Completada — 2026-09-03)
- `source/java.c` implementado con:
  - Cargador de recursos `GLResLoader` (`getResourceLength`, `getResourceBytes`, `getResourceFull`).
  - Getters de dispositivo e info de sistema de Android (`SDK_INT`, modelo `PlayStationVita`, etc.).
  - Callbacks de ciclo de vida (`Exit`, `sendAppToBackground`, `Pause`).
  - Stubs para llamadas de sonido de `GLMediaPlayer` y `MediaPlaylist`.

## Fase 7: Entrada y Bucle Principal (Completada — 2026-09-03)
- `source/main.c` reescrito con:
  - Resolución dinámica de todos los métodos nativos de Sacred Odyssey.
  - Overclocking a 444 MHz y afinidad de CPU a core 0.
  - Inicialización secuencial exacta.
  - Muestreo táctil frontal con escalado a 960x544 (`ACTION_DOWN`, `ACTION_MOVE`, `ACTION_UP`).
  - Mapeo de botones físicos (Start/Círculo a `KEYCODE_BACK`, Select a `KEYCODE_MENU`).

---

## Fase 8: Primer Arranque en Hardware — Bugs Confirmados (2026-09-03)

- **Bug 1 — `SO_PATH` apuntaba al nombre genérico del boilerplate.** `CMakeLists.txt` traía
  `SO_PATH` cacheado como `${DATA_PATH}main.so` (nombre placeholder del SoLoader boilerplate), en vez
  de `libsacredodyssey.so`. El loader nunca encontraba el `.so` real.
  - Fix: `CMakeLists.txt` → `SO_PATH` = `${DATA_PATH}libsacredodyssey.so`.
  - **Importante:** al ser una variable `CACHE STRING`, un `build/` ya configurado con el valor viejo
    no se actualiza solo editando `CMakeLists.txt` — hace falta re-correr `cmake` pasando
    `-DSO_PATH=...` explícito (o borrar la entrada del `CMakeCache.txt`) para que tome efecto.

- **Bug 2 (causa raíz confirmada con `.psp2dmp` real) — `m_gAppPath` sin el subdirectorio `GloftSOHP/`.**
  Triaje del dump `sacredodyssey-psp2core-1788489822-...`: Data abort con `PC`/`LR` dentro de `SceLibc`
  (vía `sceLibcBridge_fopen`), `R0`/`R1` en 0. El string crudo en la pila mostraba
  `ux0:data/sacredodyssey//data/3818720489.obfs` — el motor construye sus rutas de assets como
  `<m_gAppPath>/data/<id>.obfs`, replicando la estructura real de Android
  (`/sdcard/gameloft/games/GloftSOHP/data/...`, confirmado en `GameRenderer.java` con el path del video
  intro). `initPath_hook()` en `source/patch.c` seteaba `m_gAppPath` directo a `DATA_PATH`
  (`ux0:data/sacredodyssey/`), salteando el segmento `GloftSOHP/` — por eso el `fopen()` fallaba (el
  archivo real está en `ux0:data/sacredodyssey/GloftSOHP/data/*.obfs`) y el `FILE*` inválido resultante
  crasheaba más adelante dentro de `SceLibc`.
  - Fix: `source/patch.c` → `initPath_hook()` ahora arma `m_gAppPath` como `"%sGloftSOHP/"` (sin tocar
    `DATA_PATH`, que sigue usándose tal cual para `res/` vía `GLResLoader` en `source/java.c`).

- **Splash giratorio de vitaGL al arrancar.** No es código de este repo — es el "boot splashscreen"
  que vitaGL agregó (commit `e9ba795`, "Implement a boot splashscreen designed to hide loading times").
  Se compila condicionalmente con la macro `NO_SPLASHSCREEN=1` del `Makefile` de vitaGL, así que solo
  se puede quitar recompilando vitaGL (no esta app) con ese flag.
  - Se recompiló vitaGL con `NO_SPLASHSCREEN=1` y se reinstaló sobre
    `~/vitasdk/arm-vita-eabi/{lib/libvitaGL.a,include/vitaGL.h}` (afecta a todos los ports que usen
    este VitaSDK, no solo este). Se guardó backup en `libvitaGL.a.bak-presplash`.
  - **Ojo con el HEAD del repo:** el HEAD de vitaGL (commit `a3d368d`, "Add Razor shader association
    support") agrega `shark_set_shader_association_path`, que el `libvitashark.a` instalado en este
    VitaSDK no exporta todavía → error de link (`undefined reference`). Se usó el commit anterior
    (`f4b23b6`), que ya tiene el flag `NO_SPLASHSCREEN` pero no esa dependencia nueva de vitashark.
  - Verificado: build completo del proyecto (`/tmp/sacredodyssey-build`) linkea limpio y el binario
    final ya no contiene los símbolos `invoke_splashscreen`/`vitaGL Splashscreen`.

## Fase 8 (cont.): Segundo crash tras el fix de `m_gAppPath` (2026-09-03)

El fix de `m_gAppPath` funcionó (ya no crashea en la carga de `.obfs`) — el juego avanzó más y
crasheó en un punto distinto: dump `sacredodyssey-psp2core-1788491351-...`, dentro de
`glitch::video::createOpenGLES2Driver` (construcción del driver GL ES2 del motor).

- **Mismo `PC`/`LR` EXACTO que el crash anterior** (`0x815c7e88` / `0x815c4c2d`, dentro de `SceLibc`),
  con `R0`/`R1` en 0 otra vez, pero esta vez el call site no tiene nada que ver con `fopen()` — es
  durante la construcción de un objeto C++ (vtable de `CCommonGLDriverBase` visible en varios
  registros). Que sea la misma dirección desde dos call sites tan distintos apunta a una función
  interna de `SceLibc`/`SceLibcBridge` de uso muy genérico (candidato: `__cxa_guard_acquire` o un
  lock/heap interno compartido por muchos paths del runtime C++), pero **no se pudo confirmar cuál
  exactamente** sin acceso al binario real de `SceLibc` (módulo de sistema cerrado, sin símbolos) ni
  un log de texto que muestre qué estaba pasando justo antes.
- **No adiviné un fix a ciegas para este crash** — el propio `triage_summary.md` tampoco encontró
  coincidencias. Corresponde volver a triar con un log real (ver abajo) antes de tocar nada más acá.

## Fase 8 (cont.): Logging incremental a archivo + UDP en vivo (2026-09-03)

Hasta ahora el logger (`source/utils/logger.c`) solo escribía a `sceClibPrintf` (consola de debug,
invisible en consola real sin cable/plugin) — por eso `ux0_data/sacredodyssey/logs/` seguía vacío
pese a los dos crashes ya capturados. Se agregó:

- **Archivo incremental por ejecución**: `DATA_PATH"logs/log_<fecha>_<hora>.txt"`, abierto/escrito/
  cerrado (`sceIoOpen`+`sceIoWrite`+`sceIoClose`) en cada línea de log — sin buffer que perder si
  crashea justo después (patrón de `hardware_debugging.md`).
- **UDP en vivo compatible con `psvita-toolkit logs-live`**: una línea de texto plano por datagrama,
  con marcador de severidad entre corchetes (`[INFO]`, `[ERROR]`, etc. — exactamente la convención que
  espera `debugnet_server.py`), enviada por broadcast (`255.255.255.255:9999`) para no tener que
  configurar la IP de la PC a mano. `sceNetCtlInit`/`sceNetInit` se inicializan de forma perezosa en el
  primer log (si no hay red, el socket queda en `-1` y el resto de los logs simplemente no intentan
  reenviar — nunca bloquea ni hace fatal la app).
- Agregado `SceNet_stub`/`SceNetCtl_stub` al link en `CMakeLists.txt`.
- **Importante para la próxima corrida**: `l_debug`/`l_info`/`l_warn`/`l_success`/`l_wait` siguen
  compilándose a nada si no se define `DEBUG_SOLOADER` (activo solo con
  `-DCMAKE_BUILD_TYPE=Debug`/preset `debug` del toolkit) — **las dos corridas anteriores fueron con
  ese flag apagado**, por eso no había rastro ni siquiera de los `l_info` existentes (como el propio
  `fopen(...)` que hubiera confirmado el bug de `m_gAppPath` antes de necesitar el `.psp2dmp`). Para
  triar el próximo crash con contexto completo, compilar con el preset `debug`.
- Validado con build completo (release y debug) en `/tmp/sacredodyssey-build`: compila y linkea limpio.

## Fase 8 (cont.): Bug propio en el logging recién agregado (2026-09-03)

El log incremental sí escribió a archivo (`log_20260903_232328.txt`: una sola línea, "Starting Sacred
Odyssey loader initialization...", después nada) pero causó un crash NUEVO, propio de este loader (no
del juego): dump `sacredodyssey-psp2core-1788492208-...`, `Prefetch abort` con `PC = 0x0`, `LR` dentro
de `_log_net_init` (`logger.c:106`), `R12` apuntando al stub de `sceNetCtlInit`.

- **Causa confirmada:** a diferencia de `SceCtrl`/`SceDisplay`/etc., el módulo `SceNet` **no se carga
  automático** en Vita — llamar a cualquier `sceNet*`/`sceNetCtl*` sin antes
  `sceSysmoduleLoadModule(SCE_SYSMODULE_NET)` salta a través de un stub sin resolver (puntero `NULL`) y
  crashea con `PC = 0x0`. `_log_net_init()` llamaba `sceNetCtlInit()` directo, sin cargar el módulo.
- Fix: `source/utils/logger.c` → `_log_net_init()` ahora llama
  `sceSysmoduleLoadModule(SCE_SYSMODULE_NET)` primero (con `sceSysmoduleIsLoaded` para no repetirlo)
  y, si falla, deja el logging de red desactivado (archivo/consola siguen andando) en vez de reintentar
  en cada línea. Agregado `SceSysmodule_stub` al link en `CMakeLists.txt`.
- Validado: build limpio (`/tmp/sacredodyssey-build`, Debug), `sceSysmoduleLoadModule`/
  `sceSysmoduleIsLoaded` resueltos en el binario final.

## Fase 8 (cont.): Causa raíz REAL del crash de `createOpenGLES2Driver`, confirmada con log (2026-09-03)

Con el logging incremental ya funcionando (`log_20260903_232835.txt`), el log mostró exactamente qué
pasaba justo antes del crash `sacredodyssey-psp2core-1788492518-...`:

```
[WARN] fopen(ux0:data/sacredodyssey/GloftSOHP//data/3d/effects/UnlitVertexColorVP.glsl, rb): 0x0
[WARN] fopen(ux0:data/sacredodyssey/GloftSOHP//data/2181175739.obfs, rb): 0x0
[WARN] fopen(ux0:data/sacredodyssey/GloftSOHP//data/3d/effects/UnlitVertexColorFP.glsl, rb): 0x0
[WARN] fopen(ux0:data/sacredodyssey/GloftSOHP//data/2173835691.obfs, rb): 0x0
```

- **Causa confirmada:** el shader "UnlitVertexColor" (vertex + fragment) no existe en NINGUNA fuente de
  datos disponible — se comparó el set de `.obfs` desplegado contra los dos zips originales
  (`Sacred Odyssey-v1.0.6-agmod.org-apk Data.zip` y `Sacred-Odyssey-The-Raise-of-Ayden-v1.0.1-Adreno.zip`):
  **los tres tienen exactamente los mismos 31 `.obfs`**, ninguno con los IDs `2181175739`/`2173835691`,
  y ningún `.glsl` suelto en `data/3d/effects/`. Es un asset genuinamente faltante, no un bug de rutas.
- El propio `.so` SÍ tiene un fallback para esto: se encontró el string literal
  `"Pink Bad Shader"`/`"invalid pink stuff"` junto al shader completo (`attribute highp vec4 Vertex; ...
  gl_Position = WorldViewProjectionMatrix * Vertex;` / `gl_FragColor = vec4(0.8, 0.3, 0.5, 1.0);`),
  usado como reemplazo visual cuando un shader no carga — pero el código no llega a usarlo cuando TANTO
  el archivo suelto COMO el `.obfs` fallan; en cambio desreferencia algo nulo y crashea (mismo `PC`/`LR`
  de `SceLibc` que los dos crashes anteriores, confirmando que es la misma familia de bug: un `FILE*`
  NULL de un `fopen()` fallido, usado sin chequear).
- **Fix (`source/patch.c`):** en vez de adivinar el GLSL real de "UnlitVertexColor" (arriesgando bindings
  de atributos incorrectos), `initPath_hook()` ahora siembra los dos `.glsl` sueltos faltantes con el
  contenido EXACTO del "Pink Bad Shader" del propio juego (extraído verbatim del `.so` con `strings`),
  solo si el archivo todavía no existe (no pisa un asset real si aparece en un dataset más completo más
  adelante). Así el primer `fopen()` (archivo suelto) tiene éxito, nunca se llega al camino que
  crashea, y el material se ve como el rosa de "shader faltante" que el motor ya contempla — visualmente
  incorrecto pero estable.
- Validado: build limpio en `/tmp/sacredodyssey-build`.

## Fase 8 (cont.): Confirmado en consola — se repite con OTRO shader; se rediseña el fix (2026-09-03)

El fix de `UnlitVertexColorVP/FP.glsl` funcionó (log `log_20260903_234632.txt`: ambos abren bien ahora),
pero el juego avanzó y pisó el MISMO patrón con un shader distinto: `UnlitMaterialColorVP/FP.glsl` +
sus `.obfs` (`3041663596`/`3034323548`) también faltan — mismo `PC`/`LR` de `SceLibc` de siempre,
esta vez en `glitch::video::CMaterial::deserializeAttributes`.

- **Investigación cruzada con `Dungeon-Hunter-2-vita`** (mismo motor Glitch, confirmado en
  `PORTING_PLAN.md`): ese port documenta el MISMO tipo de bug (assets genuinamente faltantes de
  cualquier fuente disponible — en su caso texturas de personaje, no shaders) y confirma
  independientemente el mismo hallazgo del "Pink Bad Shader"/"PinkBadShaderFS/VS" como el fallback
  nativo del motor para shaders faltantes. Su fix establecido para esta clase de bug: **redirigir
  dentro de `fopen_soloader` (`source/reimpl/io.c`)** hacia un archivo sustituto real, mismo estilo que
  sus redirects `pvr2_env_*_alpha.tga`/`droidsans.ttf`→`DejaVuSans.ttf` — NO sembrar archivos desde
  `patch.c` (que fue mi primer intento, ahora revertido).
- **Fix rediseñado para consistencia con esa convención ya probada:**
  - `extras/PinkBadShaderVP.glsl` / `extras/PinkBadShaderFP.glsl` — el "Pink Bad Shader" del propio
    `.so`, empaquetados en `app0:` vía `vita_create_vpk` (mismo mecanismo que `extras/cpuinfo`).
  - `source/reimpl/io.c` → `fopen_soloader()`: si falla el `fopen()` real Y la ruta es
    `.../effects/*VP.glsl` o `*FP.glsl`, redirige a `app0:PinkBadShaderVP.glsl`/`FP.glsl`. Generalizado
    (no atado a un nombre de shader específico) porque ya se confirmaron DOS shaders distintos con el
    mismo patrón — bien evidenciado, no una suposición.
  - `source/patch.c` vuelto a su forma simple (sin `seed_missing_shader`).
- **Bug secundario encontrado en el mismo log (no fatal, pero ruidoso):** el caché de shaders GLSL
  compilados (`DUMP_COMPILED_SHADERS`) fallaba en cada arranque (`file_save: Could not open ...
  ux0:data/sacredodyssey/gxp/....gxp`) porque nadie creaba el directorio `gxp/`. Las otras dos ramas de
  `load_shader()` en `glutil.c` (variantes CG/GXP) sí llaman `file_mkpath()` antes de `file_save()`; la
  rama GLSL activa (`SHADER_FORMAT=GLSL`, la que usa este port) no lo hacía. Agregado el
  `file_mkpath()` faltante en `glCompileShader_soloader()`.
- Validado con build completo y verificación del `.vpk` (`unzip -l`): `PinkBadShaderVP.glsl`/
  `PinkBadShaderFP.glsl` empaquetados correctamente en `app0:`.

## Fase 8 (cont.): Sin crash, pero cuelgue tras el LiveArea (2026-09-04)

`log_20260904_000642.txt`: el redirect de shaders funcionó (`UnlitVertexColorVP/FP.glsl` abren bien,
sin los errores de `gxp/` de la vez pasada — el `file_mkpath()` agregado también sirvió), pero el log
se corta justo después, sin generar ningún `.psp2dmp` — un cuelgue, no un crash (ver
`hardware_debugging.md`: "el log también se corta, pero... señal de que algo espera un evento que nunca
llega").

- **Hallazgo clave:** `gl_preload()` (`glutil.c`) ya tenía `vglSetSemanticBindingMode(VGL_MODE_POSTPONED)`
  activo, lo que — según el propio `README VITAGL.md` — mueve la traducción real de GLSL→GXM
  (ShaccCg/vitashark) de `glCompileShader` a `glLinkProgram`. Pero `glLinkProgram` estaba mapeado
  DIRECTO a la función real de vitaGL en `dynlib.c` (sin wrapper, sin logging) — es decir, el trabajo
  pesado real ocurre exactamente donde no teníamos ninguna visibilidad, y el corte del log coincide con
  llegar ahí por primera vez en toda la sesión (primer shader que el motor compila/linkea de verdad, ya
  que en corridas previas el `fopen()` fallaba antes de siquiera llegar a esta etapa).
- **Instrumentación agregada** (no un fix todavía, sino la forma de confirmar la hipótesis con el
  próximo log): `glLinkProgram_soloader()` nuevo en `glutil.c`/`glutil.h`, wireado en `dynlib.c` en vez
  del `glLinkProgram` crudo — loguea antes/después del link real. También se agregó logging
  antes/después de `glCompileShader` real (aunque con `VGL_MODE_POSTPONED` es probable que no haga
  trabajo pesado ahí).
- **Fix defensivo aplicado de una vez, sin esperar confirmación** (bajo riesgo, cero downside): el
  fragment shader `extras/PinkBadShaderFP.glsl` (el shader de fallback que armamos) no tenía
  `precision mediump float;` — un fragment shader GLSL ES 1.00 sin qualifier de precisión por defecto
  y sin qualifiers explícitos en cada literal flotante es inválido según el spec (aunque muchos
  compiladores toleran esto). Agregado por las dudas, ya que es la primera vez que este string
  sintetizado por nosotros (no verificado contra el compilador real) llega a compilar de verdad.
- Validado con build completo; `unzip -p` confirma el `.glsl` actualizado empaquetado en el `.vpk`.

## Fase 8 (cont.): Causa real del cuelgue encontrada — `glsl.config` faltante (2026-09-04)

`log_20260904_005023.txt` confirmó que la hipótesis de `glLinkProgram` estaba mal: mi log nuevo
(`glCompileShader(...): compiling...`) NUNCA aparece — el cuelgue pasa ANTES de llegar siquiera a
`glCompileShader_soloader`, justo después de que falla `fopen(.../glsl.config)`.

- **Causa confirmada por pseudo-C (`out_ghidra.c`):** `glitch::video::CGLSLShaderManager::
  initAdditionalConfig()` abre `"glsl.config"` vía un método virtual; si vuelve NULL, Ghidra marca la
  rama siguiente como "Subroutine does not return" — no hay un manejo gracioso de "archivo no
  encontrado" para este archivo en particular (a diferencia de los shaders individuales, que sí lo
  tienen). Coincide exactamente con la evidencia real: no hay crash (no genera `.psp2dmp`), simplemente
  no vuelve.
- **Confirmado con el port hermano:** la investigación en `Dungeon-Hunter-2-vita/PORTING_PLAN.md` sobre
  su `shaders.pak` real dice explícitamente que contiene "dos archivos vacíos marcadores
  `cg.config`/`glsl.config`" junto a los 34 `.glsl` — es decir, se espera que `glsl.config` EXISTA
  (vacío) como parte del set base de assets. Nuestro dataset nunca lo tuvo — mismo patrón de "dump de
  datos incompleto" que los shaders faltantes.
- **Fix:** `extras/glsl.config` (0 bytes) empaquetado en `app0:` igual que los shaders de fallback.
  `source/reimpl/io.c` → `fopen_soloader()` redirige `glsl.config` faltante a `app0:glsl.config` en vez
  de dejarlo en NULL.
- Validado: build limpio, `unzip -l` confirma `glsl.config` (0 bytes) empaquetado en el `.vpk`.

## Fase 8 (cont.): El redirect de `glsl.config` no llegaba a ningún lado — bug propio (2026-09-04)

`log_20260904_005449.txt`: el redirect SÍ se disparó ("Missing ...glsl.config -- redirecting..."), pero
el propio redirect falló: `fopen(app0:glsl.config, rb): 0x0`. Causa: escribí el path como
`"app0:glsl.config"` (sin `/` después de los dos puntos), mientras que el único patrón ya probado y
funcionando en este mismo archivo (`"app0:/cpuinfo"`, `"app0:/meminfo"`) SÍ lleva la barra. Corregidos
los tres redirects nuevos (`PinkBadShaderVP.glsl`, `PinkBadShaderFP.glsl`, `glsl.config`) para usar
`"app0:/..."` consistente con la convención ya confirmada. Build limpio.

## Fase 8 (cont.): Recursión infinita propia + sospecha de deploy incompleto (2026-09-04)

`log_20260904_005821.txt`: el fix de la barra funcionó sintácticamente (ya loguea
`app0:/glsl.config`), pero ese `fopen()` TAMBIÉN falla — y como el basename de
`"app0:/glsl.config"` es igual a `"glsl.config"`, el redirect se llama a sí mismo para siempre
(bug propio: recursión infinita, confirmado por cientos de líneas idénticas repetidas en el log, sin
crash, colgado).

- **Fix:** guard en `fopen_soloader()` — los redirects solo se evalúan si `filename` no empieza ya con
  `"app0:"` (evita que un redirect fallido se dispare sobre sí mismo, aplica también al de los
  shaders por las dudas).
- **Sospecha principal (no confirmable desde acá, requiere acción del usuario):** el `.vpk` armado en
  esta sesión SÍ empaqueta `glsl.config`/`PinkBadShaderVP.glsl`/`FP.glsl` en `app0:` (confirmado con
  `unzip -l`), pero si el deploy a la consola usó `psvita-toolkit deploy --eboot` (sube solo el
  `eboot.bin`, más rápido) en vez de `deploy --vpk` (reinstala el `.vpk` completo), el contenido nuevo
  de `app0:` nunca llega a la consola — solo el código. Esto explicaría exactamente el fallo:
  `app0:/glsl.config` no existe todavía ahí. **Antes de seguir investigando el código, reinstalar el
  `.vpk` completo (no solo el eboot) y volver a probar.**

## Fase 8 (cont.): El fix de `glsl.config` era el problema — revertido con evidencia comparativa (2026-09-04)

`log_20260904_010252.txt`: tras reinstalar el `.vpk` completo, `app0:/glsl.config` finalmente abrió y
cerró bien (`fopen(app0:/glsl.config, rb): 0x81720250` → `fclose(...): 0`), confirmando que el
redirect en sí funcionaba. Pero el log se sigue cortando ahí mismo, un poco más adelante — sin llegar
nunca a loguear `glCompileShader(...)`.

- **Comparación línea por línea contra `log_20260903_234632.txt`** (el run MÁS AVANZADO de toda la
  sesión, de antes de tocar `glsl.config`): en ESE run, `fopen(glsl.config)` fallaba (`0x0`) y el
  motor seguía perfecto — pasaba directo a los errores de `file_save` del caché de shaders (evidencia
  de que `glCompileShader` SÍ se ejecutó). Es decir: **el motor ya maneja bien que `glsl.config` no
  exista** — la rama "Subroutine does not return" que vimos en Ghidra es, confirmado empíricamente,
  un artefacto del decompilador (no es un assert/panic real), tal como se sospechaba pero no se había
  podido confirmar antes.
- **Conclusión:** mi fix anterior estaba diagnosticado al revés. Proveer el archivo (aunque vacío) hace
  que el motor entre a la rama de "archivo SÍ existe" (`getSize()`→`alloc()`→`read()`→segunda llamada
  a `drop()`) — una rama que, con los datos de este proyecto, normalmente NUNCA se ejecuta porque el
  archivo siempre falta. Esa rama es la que realmente cuelga, no la ausencia del archivo.
- **Revertido:** el redirect de `glsl.config` en `source/reimpl/io.c` fue eliminado (dejando un
  comentario extenso explicando por qué, con las dos corridas de log como evidencia, para no
  reintroducirlo sin evidencia nueva). Se quitaron `extras/glsl.config` y su entrada en
  `vita_create_vpk`.
- Validado con build limpio.

## Fase 8 (cont.): Causa Raíz Confirmada del Cuelgue en el LiveArea Splash (2026-09-04)

Al analizar la secuencia de logs contra el código decompilado de `libsacredodyssey.so` y compararlo con el port hermano `Dungeon-Hunter-2-vita`:

1. **Caché GXP Corrupto por `DUMP_COMPILED_SHADERS` en `VGL_MODE_POSTPONED`:**
   - En `gl_preload()`, se activa `vglSetSemanticBindingMode(VGL_MODE_POSTPONED)`. Bajo este modo, vitaGL pospone la compilación y traducción de GLSL a GXM hasta la fase de `glLinkProgram()`.
   - Como resultado, en `glCompileShader_soloader()`, la llamada a `vglGetShaderBinary(shader, ...)` falla con `GL_INVALID_OPERATION` (código 0x502) y deja la variable `len` sin inicializar (basura de la pila).
   - En ejecuciones previas, `file_mkpath()` + `file_save()` volcaron esa longitud basura en archivos `.gxp` corruptos dentro de `ux0:data/sacredodyssey/gxp/`.
   - En ejecuciones subsecuentes, `load_shader()` detectó la existencia de esos `.gxp`, los leyó con `file_load()`, los inyectó a `glShaderBinary()` y puso `skip_next_compile = GL_TRUE`. El motor luego comprobó el estado de compilación (`glGetShaderiv(..., GL_COMPILE_STATUS)`), el cual devolvió `GL_FALSE`. Por consiguiente, `compileShader()` falló, `createShaderImpl()` abortó la creación del shader program y la app quedó colgada indefinidamente antes de realizar el primer `gl_swap()`, dejando la consola atascada en el splash del LiveArea.
   - **Corrección aplicada:** Se desactivó `DUMP_COMPILED_SHADERS` en `CMakeLists.txt` (exactamente igual que en *Dungeon Hunter 2*), se eliminó la lógica de volcado defectuoso de `glCompileShader_soloader()`, y se agregó en `gl_preload()` la función `cleanup_corrupt_gxp()` para purgar automáticamente cualquier `.gxp` corrupto preexistente en la tarjeta de memoria de la consola.

2. **Protección contra re-inicialización en `gl_init()`:**
   - Se añadió un guard de inicialización idempotente (`static int vgl_initialized`) en `gl_init()`. De acuerdo con la experiencia documentada en *Dungeon Hunter 2*, invocar `vglInitExtended()` repetidamente mientras el contexto `sceGxm` sigue activo congela la cola de despliegue en la pantalla de carga.

3. **Independencia total en shaders fallback (`UnlitMaterialColorVP/FP.glsl`):**
   - Los shaders `UnlitMaterialColorVP.glsl` y `UnlitMaterialColorFP.glsl` faltan en todos los datasets de Android conocidos, disparando una excepción de acceso inválido a memoria (*Data Abort*) en `CMaterial::deserializeAttributes`.
   - El redirect anterior dependía de `app0:/PinkBadShader*.glsl`, que no existía si se realizaba el despliegue rápido de `eboot.bin` vía FTP sin reinstalar el VPK completo.
   - **Corrección aplicada:** Se embebió el código fuente GLSL del *Pink Bad Shader* directamente en `source/reimpl/io.c`. Al solicitarse un shader de efectos ausente, el cargador crea automáticamente los archivos en `ux0:data/sacredodyssey/PinkBadShader*.glsl` (ubicación siempre accesible y con permisos de escritura) y los abre sin riesgo de retornar NULL.

4. **Validación de compilación:**
   - Binarios `eboot.bin` (561 KB) y `sacredodyssey.vpk` (648 KB) recompilados de forma limpia y colocados en la raíz y en `build/`.

---

## Fase 8 (cont.): Triaje del Crash `sacredodyssey-psp2core-1788499808-...` (2026-09-04)

Con el fix del LiveArea splash y los shaders fallback en `ux0:data/`:
- **Progreso excelente:** El log `log_20260904_013001.txt` demostró que el cargador superó completamente el LiveArea splash, purgó el caché corrupto `.gxp`, creó los fallbacks y compiló/linkeó **5 shaders programs completos** (programas 1 al 5).
- **Nuevo crash analizado:** Dump `sacredodyssey-psp2core-1788499808-0x0008d33363-eboot.bin.psp2dmp`:
  - `PC = 0x981c2bbc` / `LR = 0x981c2b9c` (Base del `.so` = `0x98000000` -> Offset `0x001c2bbc`).
  - Ubicación: `ShowItemEffectProxy::ShowItemEffectProxy()`.
  - **Causa raíz:** `ShowItemEffectProxy` consulta el ID del parámetro 6 (`MaterialColor`/`DiffuseColor`) mediante `CMaterialRenderer::getParameterID(6, 0)`. Al utilizar un shader que no define ese uniform, la función devuelve `0xffff` (no encontrado).
    El desensamblado del motor en `0x001c2bac`:
    ```arm
    1c2bac: cmp  r2, r1        ; r2 = número de parámetros, r1 = 0xffff
    1c2bb0: movls r3, #0       ; r2 <= r1 -> r3 = 0 (NULL)
    1c2bb4: ldrhi r3, [r3, #32]
    1c2bb8: addhi r3, r3, r1, lsl #4
    1c2bbc: ldrb r3, [r3, #6]  ; ¡CRASH! Desreferencia incondicional de NULL + 6 (0x6)
    ```
    A diferencia de todas las demás funciones del motor (que hacen `cmp r0, #0xffff; beq ...`), `ShowItemEffectProxy` cometió un error del desarrollador original de Gameloft al poner `movls r3, #0` y caer de lleno en la desreferencia.
  - **Corrección aplicada:** En `source/patch.c`, se parcheó el offset `0x001c2bb0` de `libsacredodyssey.so` sustituyendo `movls r3, #0` (`0x93a03000`) por `bls 0x1c2bec` (`0x9a00000d`). Si el parámetro no está presente, salta directamente a `0x001c2bec` omitiendo `setParameter` sin crashear.
  - Validado: Recompilación exitosa de `eboot.bin` y `sacredodyssey.vpk`.

---

## Fase 8 (cont.): Triaje del Crash `sacredodyssey-psp2core-1788500351-...` (2026-09-04)

- **Comportamiento en consola:** El parche de `ShowItemEffectProxy` funcionó perfectamente (línea 10 del log: `Patched ShowItemEffectProxy missing parameter crash`). El juego superó la inicialización de `EffectManager` e inició la carga de `CustomEffect.bdae`.
- **Análisis del nuevo crash:** Dump `sacredodyssey-psp2core-1788500351-0x00124723d5-eboot.bin.psp2dmp`:
  - `PC = 0x815c7e88` / `LR = 0x815c4c2d` (SceLibc) con `R0=0`, `R1=0`. Mismo punto de falla en `CMaterial::deserializeAttributes` al intentar deserializar un shader no cargado.
  - El log `log_20260904_013904.txt` (líneas 176-179) mostró:
    ```
    fopen(.../WaterDisturbVS.glsl, rb): 0x0
    fopen(.../466098197.obfs, rb): 0x0
    fopen(.../WaterDisturbPS.glsl, rb): 0x0
    fopen(.../463345679.obfs, rb): 0x0
    ```
  - **Causa raíz:** La condición de redirección en `source/reimpl/io.c` solo reconocía sufijos `*VP.glsl` y `*FP.glsl`. Los shaders de `CustomEffect.bdae` utilizan la nomenclatura `*VS.glsl` (Vertex Shader) y `*PS.glsl` (Pixel Shader), por lo que devolvieron NULL y provocaron el Data Abort.
  - **Análisis exhaustivo de shaders en los .bdae:** Se extrajeron todos los strings `.glsl` de `DefaultEffects.bdae` y `CustomEffect.bdae`:
    `bloomVertex.glsl`, `bloomblur0.glsl`, `bloomblur1.glsl`, `bloomblend.glsl`, `RadialBlurVP/FP.glsl`, `WaterDisturbVS/PS.glsl`, `MagmaFluidVS/PS.glsl`, `fadeOutVS/FP.glsl`, `Unlit*VP/FP.glsl`.
  - **Corrección aplicada:** En `source/reimpl/io.c`, se amplió la detección para abarcar cualquier archivo `.glsl` bajo `/effects/`: si contiene `VP.glsl`, `VS.glsl` o `Vertex.glsl` se redirige a `PinkBadShaderVP.glsl`, y todo el resto se redirige a `PinkBadShaderFP.glsl`.
  - Validado: Recompilación limpia y actualización de `eboot.bin` y `sacredodyssey.vpk`.

---

## Fase 8 (cont.): Triaje del Crash `sacredodyssey-psp2core-1788500500-...` (2026-09-04)

- **Comportamiento en consola:** ¡Avance monumental! El log `log_20260904_014131.txt` confirmó que la redirección de shaders de `CustomEffect.bdae` funcionó a la perfección:
  - Se compilaron y linkearon exitosamente los 9 shader programs del juego (`Unlit*`, `WaterDisturb`, `MagmaFluid`, `fadeOut`).
  - Se completó la inicialización de `Device` y `GameRenderer`.
  - Se inicializó `GLMediaPlayer`, `MediaPlaylist` y avanzó hasta `Game.nativeInit`.
- **Análisis del nuevo crash:** Dump `sacredodyssey-psp2core-1788500500-0x00098a3ef3-eboot.bin.psp2dmp`:
  - `PC = 0x983dd010` / `LR = 0x983dd0fc` (Base `0x98000000` -> Offset `0x003dd010`).
  - Símbolo en `0x003dd010`: `ALicenseCheck::CallJNIFuncChar(_jclass*, _jmethodID*, char*, int)`.
  - `SP = 0x81500da0`, `R3 = 0x40000` (256 KB), `Stop reason: 0x30004 (Data abort)`.
  - **Causa raíz:**
    1. Al final de `Java_com_gameloft_android_ANMP_GloftSOHP_ML_Game_nativeInit` (`0x001baa9c`), el código original ejecuta `b ALicenseCheck_InitLicense` (`0x003dd330` -> `0x003dd174`).
    2. `ALicenseCheck::Init` ejecuta `GetStaticMethodID` buscando funciones de DRM en la Activity (`d`, `db`, `dc`, `da`), y luego salta a `ALicenseCheck::LoadConfig()` (`0x003dd0a4`).
    3. `LoadConfig` resta `0x40000` (256 KB) directos al `sp` para decodificar la clave de licencia en stack y llama a `CallJNIFuncChar` (`0x003dd010`).
    4. La primera instrucción de `CallJNIFuncChar` es `push {r4-r8, lr}`, la cual desborda la pila de la hebra principal del sistema Vita (provocando el Data Abort).
- **Correcciones aplicadas:**
  1. **Bypass total de `ALicenseCheck` (`source/patch.c`):**
     - Se hookearon `ALicenseCheck_InitLicense`, `_ZN13ALicenseCheck4InitEP7_JNIEnvP7_jclass`, `_ZN13ALicenseCheck10LoadConfigEv` y `_ZN13ALicenseCheck14ValidateServerEb` con stubs que retornan inmediatamente.
     - Al stubear la inicialización de la licencia, `nativeGameInit` retorna limpiamente sin invocar llamadas JNI inexistentes de DRM ni agotar la pila.
  2. **Aumento de la Pila Principal (`source/main.c`):**
     - Se definió `unsigned int sceUserMainThreadStackSize = 1 * 1024 * 1024;` (1 MB de pila) para prevenir desbordamientos en cualquier cálculo intensivo o recursivo del juego.
  3. **Stubs JNI de `MediaPlaylist` (`source/java.c`):**
     - Se implementó `empty_byte_array` (`jda_alloc(0, FIELD_TYPE_BYTE)`) para `GetPlayListName`/`getPlayListName`.
     - Se mapearon `getListID` y `hasSDCard` a `MID_INTEGER_ZERO`.
  4. **Validación:**
     - Compilación limpia 100%. Generados nuevos [`eboot.bin`](file:///Volumes/Seagate/PSVITA%20Develop/Sacred-Odyssey-vita/eboot.bin) (561 KB) y [`sacredodyssey.vpk`](file:///Volumes/Seagate/PSVITA%20Develop/Sacred-Odyssey-vita/sacredodyssey.vpk) (648 KB).

---

## Fase 8 (cont.): Triaje del Crash `sacredodyssey-psp2core-1788500979-...` (2026-09-04)

- **Comportamiento en consola:** ¡Se alcanzó el bucle principal de renderizado!
  - `log_20260904_014930.txt` confirmó que el bypass de `ALicenseCheck` funcionó al 100%.
  - Se ejecutó `GameRenderer.nativeResize(960x544)`.
  - Se ingresó a `Entering main render loop...` y comenzó el primer frame en `nativeGameRendererRender()`.
- **Análisis del nuevo crash:** Dump `sacredodyssey-psp2core-1788500979-0x000a3b3431-eboot.bin.psp2dmp`:
  - `PC = 0x981bdf84` / `LR = 0x810969f9` (Base `.so` = `0x98000000` -> Offset `0x001bdf84`).
  - Símbolo: `Application::Update()` (`_ZN11Application6UpdateEv`).
  - `R3 = 0x0`, `Stop reason: 0x30004 (Data abort)`.
  - Desensamblado en `0x001bdf80`:
    ```arm
    1bdf80: ldr r3, [r3]          ; r3 = Gameplay::s_instance (NULL en arranque/menús!)
    1bdf84: ldr r3, [r3, #32]     ; ¡CRASH! Desreferencia incondicional de NULL + 32
    1bdf88: cmp r3, #0
    1bdf8c: beq 1bdfac
    ```
  - **Causa raíz:** `Gameplay::s_instance` solo se crea cuando se entra a una partida 3D. Durante los menús de inicio o cinemáticas, `Gameplay::s_instance` es `NULL`. El código de `Application::Update()` asumió erróneamente que `Gameplay::s_instance` siempre existe, intentando desreferenciar su campo `m_player` (`[r3, #32]`) para leer el `timeScale`.
- **Correcciones aplicadas:**
  - **Trampolín seguro de comprobación (`source/patch.c`):**
    - Se alojó un trampolín en la región no utilizada de `ALicenseCheck::CallJNIFuncChar` (`0x003dd010`):
      ```arm
      ldr r3, [r3]
      cmp r3, #0
      beq 0x1bdfac            ; Si Gameplay::s_instance es NULL -> saltar de forma segura
      ldr r3, [r3, #32]
      cmp r3, #0
      beq 0x1bdfac            ; Si m_player es NULL -> saltar de forma segura
      vmov.f32 s14, #1.0
      movw r2, #4348
      add r2, r2, r3
      vldr s15, [r2]
      vcmp.f32 s15, s14
      vmrs APSR_nzcv, fpscr
      bne 0x1be018            ; Si timeScale != 1.0f -> escalar dt
      b 0x1bdfac              ; Si timeScale == 1.0f -> continuar
      ```
    - En `0x001bdf80`, se sustituyó `ldr r3, [r3]` por un salto directo `b 0x003dd010` (`0xea087c22`).
  - **Validación:**
    - Compilación limpia 100%. Generados nuevos [`eboot.bin`](file:///Volumes/Seagate/PSVITA%20Develop/Sacred-Odyssey-vita/eboot.bin) (562 KB) y [`sacredodyssey.vpk`](file:///Volumes/Seagate/PSVITA%20Develop/Sacred-Odyssey-vita/sacredodyssey.vpk) (648 KB).

---

## Fase 8 (cont.): Triaje del Crash `sacredodyssey-psp2core-1788501357-...` (2026-09-04)

- **Comportamiento en consola:** El parche de `Application::Update` funcionó a la perfección. La función completó el cálculo de delta time y llamó al método virtual de dibujo `Application::_Draw(int)`.
- **Análisis del nuevo crash:** Dump `sacredodyssey-psp2core-1788501357-0x0009923e37-eboot.bin.psp2dmp`:
  - `PC = 0x981bd104` / `LR = 0x981bdfc0` (Base `.so` = `0x98000000` -> Offset `0x001bd104`).
  - Símbolo: `Application::_Draw(int)` (`_ZN11Application5_DrawEi`).
  - `R2 = 0x9848416c` (`&Gameplay::s_instance`), `R3 = 0x0`, `Stop reason: 0x30004 (Data abort)`.
  - Desensamblado en `0x001bd100`:
    ```arm
    1bd100: ldr  r3, [r2]          ; r3 = Gameplay::s_instance (NULL en arranque/menús!)
    1bd104: ldrb sl, [r3, #36]     ; ¡CRASH! Desreferencia incondicional de NULL + 36
    1bd108: cmp  sl, #0
    1bd10c: beq  1bd210
    ```
  - **Causa raíz:** Mismo patrón que en `Application::Update`: `Application::_Draw(int)` consulta el flag en `[r3, #36]` de `Gameplay::s_instance` sin validar antes si la instancia de gameplay es nula.
  - **Inspección de funciones hermanas (`so-crash-triage`):** Se inspeccionaron todas las referencias a `Gameplay::s_instance` en `Application`, encontrando el mismo patrón de riesgo en `Application::Draw2D()` (`0x001bdc14`).
- **Correcciones aplicadas:**
  1. **Trampolín para `Application::_Draw` (`source/patch.c`):**
     - Ubicado en `0x003dd050`:
       ```arm
       mov    sl, #0
       cmp    r3, #0
       ldrbne sl, [r3, #36]
       cmp    sl, #0
       beq    0x1bd210
       b      0x1bd110
       ```
     - En `0x001bd104`, se sustituyó `ldrb sl, [r3, #36]` por `b 0x003dd050` (`0xea087fd1`).
  2. **Trampolín preventivo para `Application::Draw2D` (`source/patch.c`):**
     - Ubicado en `0x003dd070`:
       ```arm
       ldr    r3, [r3]
       cmp    r3, #0
       beq    0x1bdc70
       ldr    r3, [r3, #32]
       cmp    r3, #0
       beq    0x1bdc70
       b      0x1bdc24
       ```
     - En `0x001bdc14`, se sustituyó `ldr r3, [r3]` por `b 0x003dd070` (`0xea087d15`).
  3. **Validación:**
     - Compilación limpia 100%. Generados nuevos [`eboot.bin`](file:///Volumes/Seagate/PSVITA%20Develop/Sacred-Odyssey-vita/eboot.bin) (562 KB) y [`sacredodyssey.vpk`](file:///Volumes/Seagate/PSVITA%20Develop/Sacred-Odyssey-vita/sacredodyssey.vpk) (649 KB).

---

## Sesión 2026-09-04 (cont.) — Diagnóstico de pantalla negra, desbloqueo de Video/Resume y telemetría de render loop

### Hallazgos de triage cruzado con *Dungeon Hunter 2* y `.so`:
- **Causa análoga a DH2 investigada:** En DH2, la pantalla negra/aquamarine se producía porque `GSInit::Update` esperaba `videoDone = 1`, variable que nunca cambiaba porque el stub de video era un no-op y el ciclo `appPause` -> `appResume` nunca ocurría.
- **En Sacred Odyssey (`libsacredodyssey.so`):**
  - El motor gestiona el video mediante `nativePlayVideo()` -> `Game.playVideo()`.
  - Las variables globales de control en `.bss` son `isVideoFinish`, `s_bReturnFromVideo`, y `m_bIsPlayMovie`.
  - Si el juego solicita video o arranca esperando notificación, o si el bucle de renderizado se queda en negro mudo, no había ninguna traza de logging en el bucle principal.
  - La máquina de estados principal arranca en `SplashState` (estado 5 de `StateAutomat`), cuyo clear color por defecto es negro (`0xFF000000`).

### Correcciones aplicadas:
1. **Desbloqueo de variables de video (`source/main.c` y `source/java.c`):**
   - Implementado `method_play_video` en [`source/java.c`](file:///Volumes/Seagate/PSVITA%20Develop/Sacred-Odyssey-vita/source/java.c) conectado a `MID_PLAY_VIDEO` (`Game.playVideo`).
   - Al llamarse `playVideo`, se fuerza inmediatamente `isVideoFinish = 1`, `s_bReturnFromVideo = 0` y `m_bIsPlayMovie = 0` con sincronización en el `.so`.
   - Justo antes de entrar al bucle principal de renderizado en [`source/main.c`](file:///Volumes/Seagate/PSVITA%20Develop/Sacred-Odyssey-vita/source/main.c), se inicializan de forma segura estas banderas para garantizar que `appUpdate()` no quede esperando una ventana externa.
2. **Telemetría y diagnóstico en el loop (`source/main.c`):**
   - Añadido log cada 60 frames:
     `[render_diag] frame=%d fps=%.1f glGetError=0x%04x (alive=%u, paused=%u, movie=%u)`
   - Permite saber con certeza matemática si el motor corre a 60 FPS, si hay errores de OpenGL (`glGetError`), y el estado de `g_appAlive`/`g_appPaused`.
3. **Binarios generados:**
   - [`eboot.bin`](file:///Volumes/Seagate/PSVITA%20Develop/Sacred-Odyssey-vita/eboot.bin) (555 KB) y [`sacredodyssey.vpk`](file:///Volumes/Seagate/PSVITA%20Develop/Sacred-Odyssey-vita/sacredodyssey.vpk) (642 KB) actualizados y listos.

---

## Próxima Fase: Fase 8 (cont.) — Probar en consola física
- [x] Superar LiveArea splash.
- [x] Carga y compilación de los 9 shaders (`DefaultEffects` + `CustomEffect`).
- [x] Neutralizar `ALicenseCheck` y desbordamiento de stack en `nativeGameInit`.
- [x] Superar entrada al bucle principal de renderizado.
- [x] Parchear `Gameplay::s_instance` en `Application::Update()`, `Application::_Draw()` y `Application::Draw2D()`.
- [x] Implementar desbloqueo de video y telemetría de frames en el render loop.
- [ ] Transferir nuevo `eboot.bin` y capturar el log para verificar FPS, errores GL y avance del renderizado.
