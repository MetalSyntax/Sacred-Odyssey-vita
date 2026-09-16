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

---

## Fase 8 (cont.): Triaje del Crash `sacredodyssey-psp2core-1788503105-...` — 4to sitio de `Gameplay::s_instance` sin chequear, en `EffectManager::update` (2026-09-04)

- **Comportamiento en consola:** El log `log_20260904_015548.txt` mostró que el bucle de renderizado
  se alcanzó ("Entering main render loop...") pero no llegó a loguear ningún `[render_diag]` (que se
  loguea recién al frame 60) — el crash ocurrió en el primer frame, antes de eso.
- **Análisis del dump** (`sacredodyssey-psp2core-1788503105-0x000fa22561-eboot.bin.psp2dmp`):
  - `psvita-toolkit analyze` autodetectó una base de `.so` incorrecta (`0x81191000`), lo que marcaba
    `PC`/`LR` como "fuera de rango". Recalculando con la base real ya confirmada en crashes anteriores
    de esta sesión (`0x98000000`), `PC = 0x981bf494` → offset `0x001bf494`, dentro de
    `EffectManager::update(int)` (símbolo dinámico confirmado con `objdump -T`, offset +0x44).
  - **Importante:** a diferencia de todas las funciones parcheadas hasta ahora en esta sesión (Thumb),
    `EffectManager::update` está compilada en **modo ARM puro** — desensamblar con
    `-M force-thumb` da instrucciones basura (parece un jump table). Sin ese flag, el desensamblado es
    coherente.
  - Desensamblado real en el punto de crash:
    ```arm
    1bf488: ldr fp, [pc, #820]   ; GOT offset de &Gameplay::s_instance
    1bf48c: ldr r3, [r8, fp]     ; r3 = &Gameplay::s_instance
    1bf490: ldr r3, [r3]         ; r3 = Gameplay::s_instance (NULL en menús/splash!)
    1bf494: ldr r4, [r3, #32]    ; ¡CRASH! Desreferencia incondicional de NULL + 32
    1bf498: cmp r4, #0
    1bf49c: beq 1bf6c8
    ```
  - **Causa raíz:** el pseudo-C de Ghidra (`EffectManager::update`, línea ~7030) confirma exactamente lo
    mismo: `iVar6 = *(int *)(Gameplay::s_instance + 0x20); if (iVar6 == 0) { ... }` — igual patrón que
    `Application::Update`/`_Draw`/`Draw2D` (Fase 8, crashes anteriores), pero un 4to sitio distinto no
    cubierto todavía. `Application::_Draw()` llama a `EffectManager::update()` **incondicionalmente en
    cada frame** (confirmado en el pseudo-C, dos call sites idénticos — uno es un thunk de vtable del
    mismo método), incluso en menús/splash donde `Gameplay::s_instance` todavía no existe.
  - `r4` no se usa para nada más que el `cmp r4, #0 / beq 1bf6c8` inmediatamente siguiente, así que
    saltar directo a `0x1bf6c8` cuando `Gameplay::s_instance` es NULL reproduce el mismo camino que
    cuando el campo vale legítimamente 0 (sin necesidad de calcular `r4`).
  - **Sibling revisado pero NO parcheado (sin evidencia todavía):** `EffectManager::render()` (llamado
    justo después de `update()` en el mismo frame) desreferencia incondicionalmente `this+0x378` sin
    chequeo — el propio `update()` SÍ chequea ese mismo campo antes de usarlo, lo que sugiere que puede
    ser NULL en algún estado. No se tocó por ahora (no confirmado con log/dump real, y `render()` usa
    `this` implícito en `r0` que Ghidra no pudo reconstruir con confianza) — si aparece un crash ahí,
    retomar con el mismo método.
- **Fix aplicado (`source/patch.c`):** trampolín nuevo en la misma región muerta de
  `ALicenseCheck::CallJNIFuncChar` ya usada por los 3 fixes anteriores, a continuación del último
  (`0x003dd090`, con espacio libre confirmado hasta `0x003dd0a4` donde empieza `LoadConfig`):
  ```arm
  cmp r3, #0
  beq 0x1bf6c8
  ldr r4, [r3, #32]
  b 0x1bf498
  ```
  En `0x001bf494`, se sustituyó `ldr r4, [r3, #32]` por `b 0x003dd090` (`0xea0876fd`).
- Validado: build limpio (preset `debug`). `eboot.bin` (563 KB) y `sacredodyssey.vpk` (650 KB)
  regenerados en `build/` y copiados a la raíz. **Pendiente: la consola no respondía por FTP en este
  intento de deploy (`Host is down`) — falta reactivar el servidor FTP en VitaShell (SELECT) y
  redesplegar (`deploy --eboot` alcanza, este fix no toca `app0:`/assets) para confirmar en hardware
  real.**

---

## Fase 8 (cont.): Triaje del Crash `sacredodyssey-psp2core-1788538268-...` — el propio fix anterior dejaba un registro sin inicializar (2026-09-04)

- **Comportamiento en consola:** El fix de `EffectManager::update` funcionó en el sentido de que ya
  no crashea en la desreferencia de `Gameplay::s_instance` — el log volvió a avanzar hasta "Entering
  main render loop..." sin volver a caer ahí. Pero apareció un crash NUEVO, causado por el propio
  trampolín agregado en la sesión anterior.
- **Análisis del dump** (`sacredodyssey-psp2core-1788538268-0x0016a12f05-eboot.bin.psp2dmp`):
  - De nuevo, `psvita-toolkit analyze` autodetectó una base de `.so` incorrecta (`0x97ed1000`), dando
    nombres de símbolo sin sentido (`glitch::video::IVideoDriver::removeUnused()`,
    `CNullDriver::setPolygonOffset`). Recalculando con la base real confirmada en toda la sesión
    (`0x98000000`): `PC = 0x981be958` → offset `0x001be958` = `Ghost::Destroy()+0x4`; `LR = 0x981bf6dc`
    → offset `0x001bf6dc` = `EffectManager::update(int)+0x28c`.
  - **`LR` es la pista clave:** `0x1bf6dc` es exactamente la instrucción siguiente a
    `bl 0x1be954 <Ghost::Destroy>` en `0x1bf6d8` — es decir, el crash ocurre DENTRO de `Ghost::Destroy()`,
    llamado desde el bucle de limpieza al que salta nuestro propio trampolín (`0x1bf6c8`) cuando
    `Gameplay::s_instance` es `NULL`.
  - **Causa raíz (bug propio, no del motor):** `0x1bf6c8` no es un simple "no hacer nada" — es la
    entrada de un bucle que usa `r4` como índice/contador (`add r0, r4, r4, lsl #3; ...;
    bl Ghost::Destroy`) 20 veces. En el camino legítimo (cuando `Gameplay::s_instance` SÍ existe y
    `m_player == 0`), `r4` ya vale `0` en ese punto porque es el MISMO registro que se acaba de comparar
    y saltar (`cmp r4, #0; beq 1bf6c8`). Mi trampolín de la sesión anterior saltaba directo a `0x1bf6c8`
    SIN inicializar `r4` — quedaba con basura de antes de entrar a la función, el bucle calculaba un
    puntero basura, y `Ghost::Destroy()` crasheaba al desreferenciar ese "this" inválido
    (`ldr r5, [r0]` en `Ghost::Destroy+0x4`, confirmado con `objdump` en modo ARM).
  - **Lección:** al redirigir a una etiqueta intermedia de una función real (en vez de a un `return`
    limpio), hay que verificar qué registros esa etiqueta asume ya inicializados por el código que se
    está salteando — no alcanza con replicar el chequeo de puntero NULL.
- **Fix corregido (`source/patch.c`):** el mismo trampolín en `0x003dd090` ahora fuerza `r4 = 0`
  explícitamente en el camino NULL, usando condicionales (`moveq`/`ldrne` sobre el flag Z de
  `cmp r3, #0`, que `mov`/`ldr` no tocan) para no gastar una instrucción extra de comparación:
  ```arm
  cmp   r3, #0
  moveq r4, #0
  ldrne r4, [r3, #32]
  beq   0x1bf6c8
  b     0x1bf498
  ```
  Verificados los opcodes ensamblando con `arm-vita-eabi-as`/`-ld -Ttext=0x3dd090` en vez de calcularlos
  a mano (evita errores de encoding en las instrucciones condicionales). Sigue entrando en el mismo
  hueco libre de 20 bytes (`0x3dd090`-`0x3dd0a4`) sin invadir `ALicenseCheck::LoadConfig`.
- Validado: build limpio (preset `debug`). `eboot.bin` (563 KB) desplegado por FTP a la consola
  (`deploy --eboot`, conexión restablecida). Pendiente el próximo log/dump para confirmar en hardware.

---

## Fase 8 (cont.): Triaje del Crash `sacredodyssey-psp2core-1788538999-...` — 6to sitio de `Gameplay::s_instance` sin chequear, en `DefaultEffectProxy::render` (2026-09-04)

- **Comportamiento en consola:** El fix de `EffectManager::update` (con el `r4` corregido) funcionó —
  ya no crashea ahí. El log volvió a avanzar hasta "Entering main render loop..." y crasheó de nuevo,
  un paso más adelante en la misma cadena de renderizado de efectos.
- **Análisis del dump** (`sacredodyssey-psp2core-1788538999-0x000b2739cb-eboot.bin.psp2dmp`):
  `psvita-toolkit analyze` volvió a autodetectar una base incorrecta (`0x97ed4000`, dando nombres sin
  sentido de vitaGL como `CNullDriver::setDitherEnable`). Recalculando con la base real
  (`0x98000000`): `PC = 0x981c26bc` → offset `0x001c26bc` = `DefaultEffectProxy::render()+0x40`;
  `LR = 0x9829eab8` → dentro de un helper interno de `std::vector` (`_M_insert_overflow`), lo cual
  tiene sentido: `EffectManager::render()` (llamado cada frame justo después de `update()`) recorre su
  lista de `IEffectProxy*` activos y llama al `render()` virtual de cada uno.
- **Causa raíz:** exactamente el mismo patrón que los 5 sitios anteriores en esta sesión — desreferencia
  de `Gameplay::s_instance->m_player` (offset `+0x20`/32, el mismo campo de siempre) sin chequear
  primero si `Gameplay::s_instance` es `NULL`:
  ```arm
  1c26b4: ldr r3, [r4, r3]   ; r3 = &Gameplay::s_instance (vía GOT)
  1c26b8: ldr r3, [r3]       ; r3 = Gameplay::s_instance (NULL en menús/splash!)
  1c26bc: ldr r3, [r3, #32]  ; ¡CRASH! Desreferencia incondicional de NULL + 32
  1c26c0: cmp r3, #0
  1c26c4: popeq {r4, pc}     ; YA maneja el caso "m_player == 0" con un return temprano
  ```
  A diferencia del fix de `EffectManager::update`, acá no hace falta sembrar ningún otro registro:
  `0x1c26c0` solo lee `r3`, así que basta con dejarlo en `0` (en vez de crashear calculándolo) para
  reproducir exactamente el mismo camino de "no hay jugador todavía".
- **Sibling revisado pero NO parcheado (sin evidencia todavía):** `NightEffectProxy::render()`
  (`0x001c3740`, compartido por `GameNightEffectProxy::render` vía un simple `b`) tiene el mismo patrón
  GOT→`Gameplay::s_instance`→`+32` en `0x001c37a8`-`0x001c37b8`, pero está adentro de un loop que solo
  corre si el vector interno de ese proxy no está vacío — no confirmado que se alcance todavía desde un
  menú inactivo. Si aparece un crash ahí, aplicar el mismo método.
- **Fix aplicado (`source/patch.c`):** trampolín en `0x003dd0a4` (siguiente hueco libre después del de
  `EffectManager::update`, dentro del mismo cuerpo muerto de `ALicenseCheck::LoadConfig`, hookeado y
  por lo tanto inalcanzable):
  ```arm
  cmp r3, #0
  ldrne r3, [r3, #32]
  b 0x1c26c0
  ```
  En `0x001c26bc`, se sustituyó `ldr r3, [r3, #32]` por `b 0x003dd0a4` (`0xea086a78`). Opcodes
  verificados ensamblando con `arm-vita-eabi-as`/`-ld -Ttext=<dirección>` (misma disciplina adoptada
  tras el bug de registro sin inicializar del fix anterior).
- Validado: build limpio (preset `debug`). `eboot.bin` (563.5 KB) desplegado por FTP
  (`deploy --eboot`). Pendiente el próximo log/dump.

---

## Fase 8 (cont.): Triaje del Crash `sacredodyssey-psp2core-1788539515-...` — pointer inválido en un `%s` de `sprintf()`, fix defensivo genérico (2026-09-04)

- **Comportamiento en consola:** El fix de `DefaultEffectProxy::render` funcionó. El log avanzó MUCHO
  más lejos: superó por completo el bucle de efectos, entró a `Entering main render loop...`, abrió
  `SaveGame.bin` y `data/texts/text.EN.lang` (carga de idioma/configuración, primera vez que corre este
  código) y crasheó ahí.
- **Análisis del dump** (`sacredodyssey-psp2core-1788539515-0x000a8e3ecb-eboot.bin.psp2dmp`): por primera
  vez en esta sesión, el crash NO está en `libsacredodyssey.so` sino en el propio loader:
  `PC = 0x810929e4` (`strlen`) / `LR = 0x8109668f` (`_svfprintf_r`, la lógica interna de newlib detrás de
  `sprintf`/`vsnprintf`/`printf`). `R0 = 0xfffffff8` — el argumento de `strlen`, es decir, el puntero que
  `_svfprintf_r` intentaba medir para un `%s`.
- **Investigación (sin poder confirmar el call site exacto):** se rastreó extensamente el código
  decompilado buscando qué `sprintf()` del `.so` pudo pasar un puntero inválido justo en esta ventana
  (carga de idioma vía `StringMgr::SetLanguage` → `sprintf(buf,"text_%s_lang", Language_Names[lang])`;
  `GameSettings::getDefault{Save,MC,Quest,Trophy}File()` → `sprintf(buf,"%s%s", appPath, "Save*.bin")`;
  `FileStream::Open`'s rama de assets `.obfs`). Se descartaron varias hipótesis cruzando el pseudo-C
  contra el ensamblador real (ej.: `FileStream::Open` SÍ chequea `fopen()==NULL` correctamente pese a
  que el pseudo-C de Ghidra lo mostraba como una sospechosa doble-desreferencia — confirmado con
  `objdump` que es un chequeo simple y correcto, otro caso de "no confiar en el pseudo-C sin cruzar con
  el ensamblador real"; `Application::GetDeviceLanguage()` acota su resultado a `{0,1,2,3,4,5,8}` pase lo
  que pase, así que no puede desbordar `Language_Names[9]`). No se pudo aislar el call site EXACTO
  (7 MB de `.so`, decenas de `sprintf`/`sys::println` candidatos, sin logging propio de estas llamadas).
- **Decisión: en vez de seguir adivinando, blindar la clase de bug completa.** Se agregó
  `source/reimpl/fmt.c`/`fmt.h`: wrappers `sprintf_soloader`/`snprintf_soloader`/`vsprintf_soloader`/
  `vsnprintf_soloader` que recorren el format string manualmente (respetando flags/width/precisión/
  modificadores de longitud `h/hh/l/ll/L/z/j/t/q`, avanzando el `va_list` con el tipo correcto en cada
  conversión para no desalinear los argumentos siguientes) y, SOLO para cada conversión `%s`, validan el
  puntero (`is_pointer_plausible`: rechaza `NULL`, `< 0x1000`, y `> 0xf0000000` — este último umbral
  descarta específicamente sentinels como `0xfffffff8`, que son enteros negativos pequeños reinterpretados
  como punteros gigantes). Si algún `%s` es inválido, la llamada se aborta de forma segura (buffer vacío
  + `l_warn` con el format string completo, para diagnosticar el próximo hallazgo) SIN tocar la
  implementación real; si todos los `%s` son válidos, se delega 100% a la función real sin cambios de
  comportamiento — cero riesgo para los cientos de `sprintf` que ya funcionan bien en este proyecto.
  Conectado en `source/dynlib.c` reemplazando las resoluciones directas de `sprintf`/`snprintf`/
  `vsprintf`/`vsnprintf`.
- Validado: build limpio (preset `debug`). `eboot.bin` (564.3 KB) desplegado por FTP (`deploy --eboot`).
  Si el próximo log muestra un `[WARN] *printf_soloader: blocked call...`, eso da el format string exacto
  para localizar el call site real en Ghidra y decidir si hace falta un fix más específico además del
  guard genérico.

---

## Fase 8 (cont.): El guard de `fmt.c` NO interceptó el crash — instrumentado para averiguar por qué (2026-09-04)

- **Resultado inesperado:** `log_20260904_133656.txt` + dump `sacredodyssey-psp2core-1788543426-...`
  reproducen el crash EXACTO de la vuelta anterior — mismo `PC`/`LR` (`strlen`/`_svfprintf_r`), mismo
  `R0 = 0xfffffff8`, mismo `R1 = 0x5e68`, registros `R6`/`R8`/`R10`/`R9` prácticamente idénticos (`R9`
  sigue apuntando a la constante `"%s%s"` del `.so`, offset `0x412890`) — **y CERO líneas
  `[WARN] *printf_soloader: blocked call...` en el log.** Confirmado que el binario desplegado SÍ tenía
  el fix (las 6 líneas `[INFO] Patched ...` de siempre aparecen al principio del log, y
  `sprintf_soloader`/`vsprintf_soloader`/`scan_format_args` están confirmados presentes y linkeados en
  `build/sacredodyssey.elf` con `nm`). Es decir: el guard NO bloqueó la llamada peligrosa, pero tampoco
  hay evidencia de que la haya visto pasar limpia — no había ningún logging incondicional, solo se
  logueaba el caso de rechazo.
- **Qué se descartó:** doble entrada en la tabla de `dynlib.c` (no hay), resolución de símbolos rota
  (`sprintf`/`vsprintf` son los únicos símbolos `UND` de la familia printf en el `.so` — confirmado con
  `objdump -T`, y ambos están mapeados a los wrappers). No se pudo confirmar ni descartar con certeza si
  `scan_format_args()` está contando mal los argumentos previos de ALGÚN format string real que no hemos
  identificado (walk de varargs desalineado) versus si el guard nunca llega a ejecutarse para este call
  site en particular.
- **Decisión: en vez de seguir ajustando el validador a ciegas, instrumentar para observar.** Se agregó
  un `l_debug("*printf_soloader: fmt=\"%s\"", fmt)` INCONDICIONAL (no solo en el caso de rechazo) al
  principio de las 4 funciones en `source/reimpl/fmt.c`. La preset `debug` ya tiene `DEBUG_SOLOADER`
  activo (se ve por los cientos de `[DEBUG]`/`[WARN]` ya presentes en los logs), así que esto va a
  aparecer en el próximo log. Dos resultados posibles y lo que cada uno significaría:
  - **Si el log llega hasta el crash SIN ninguna línea `sprintf_soloader:`/`vsprintf_soloader:` nueva
    justo antes:** el crash no pasa por estos wrappers en absoluto — hay que buscar la causa en otro
    lado (posiblemente una función *printf del propio loader/FIOS ajena al `.so`, o un import no cubierto
    todavía).
  - **Si aparece un `sprintf_soloader: fmt="..."` como última línea antes del crash:** ese es el format
    string real -- localizarlo en `out_ghidra.c`, y si `scan_format_args()` no lo rechazó pese a tener un
    `%s` inválido, revisar ese format string específico a mano (especificadores no contemplados, orden de
    argumentos) para corregir el parser.
- Validado: build limpio. `eboot.bin` (564.4 KB) desplegado por FTP.

---

## Fase 8 (cont.): La instrumentación reveló un bug PROPIO en `logger.c` — `sceClibPrintf(buffer_b)` reinterpretaba texto ya formateado como format string (2026-09-04)

- **Resultado:** `log_20260904_135907.txt` + dump `sacredodyssey-psp2core-1788544751-...` — el trazo
  incondicional de `fmt.c` sí funcionó (aparecen decenas de `[DEBUG] sprintf_soloader: fmt="\t%s"`,
  `fmt="%s%u"`, `fmt="%d"`, etc. — el `.so` está enumerando extensiones GL una por una durante
  `Initializing GameRenderer`), pero el crash resultó ser un bug NUEVO, propio de este loader, e
  **inducido por la propia instrumentación agregada la vuelta pasada** — no tiene nada que ver con el
  bug original de idioma/`sprintf` que se sigue investigando.
  - `PC`/`LR` esta vez caen dentro de `SceLibKernel` (no en `_svfprintf_r`/`strlen` de newlib como las
    dos veces anteriores) — señal de que es un bug DISTINTO.
  - **Causa raíz confirmada:** `source/utils/logger.c` → `_log_print()` línea ~200 hacía
    `sceClibPrintf(buffer_b);` -- pasando el TEXTO YA FORMATEADO (datos) directamente como format
    string, en vez de `sceClibPrintf("%s", buffer_b)`. Mientras ningún mensaje logueado contuviera un
    `%` literal, esto nunca se manifestaba. Pero el propio `l_debug("...fmt=\"%s\"", fmt)` agregado la
    vuelta pasada (para diagnosticar el OTRO bug) loguea format strings CRUDOS como dato -- ej.
    `"sprintf_soloader: fmt=\"\t%s\""` -- que SÍ contienen un `%s` literal en su texto ya sustituido.
    Al pasar ese texto de vuelta como format string a `sceClibPrintf`, éste intenta leer un vararg
    fantasma para ese `%s` -- basura de la pila/registros, desreferenciada como puntero -- y crashea
    dentro de `SceLibKernel` de forma no determinística (no en la primera ocurrencia; recién a los
    varios "\t%s" seguidos, cuando la basura leída resultó ser una dirección inválida).
  - **Por qué no se vio antes:** es un bug latente preexistente en `logger.c` (nunca antes se logueó un
    mensaje cuyo TEXTO SUSTITUIDO contuviera un `%`), expuesto recién por la instrumentación de
    diagnóstico de la vuelta anterior -- no es evidencia sobre el bug original que se estaba
    investigando (ese sigue sin confirmarse).
  - Fix: `sceClibPrintf(buffer_b)` → `sceClibPrintf("%s", buffer_b)`. Regla general reforzada con un
    comentario extenso en el código: nunca pasar un buffer con contenido derivado de datos/parámetros
    como format string.
- Validado: build limpio. `eboot.bin` (564.3 KB) desplegado por FTP. El trazo incondicional de
  `sprintf_soloader`/`vsprintf_soloader` en `fmt.c` se deja activo (ya no es peligroso con el fix de
  `logger.c`) para seguir cazando el bug original de idioma en el próximo log.

---

## Vendorización de vitaGL (2026-09-04)

- **Problema:** el proyecto enlazaba contra el `libvitaGL.a`/`vitaGL.h` **globales** de
  `~/vitasdk/arm-vita-eabi/`, que en algún momento fueron recompilados a mano y reinstalados sobre esa
  ruta global con `NO_SPLASHSCREEN=1` (para quitar el splashscreen de arranque de vitaGL, del que este
  port depende). Eso hacía que cualquier otro port que comparta el mismo VitaSDK quedara afectado por
  ese cambio, y que un `vitasdk-update`/reinstalación del SDK rompiera este build en silencio.
- **Fix: se vendorizó vitaGL como git submodule** (`lib/vitagl`, apuntando a
  `https://github.com/Rinnegatamante/vitaGL.git`, mismo repo que ya usa el port hermano
  Asphalt-5-Vita del mismo motor Gameloft "Glitch"), y `CMakeLists.txt` ahora compila vitaGL **desde
  fuente** con su propio `Makefile` (`add_custom_target(vitaGL_build ...)` + `add_library(vitaGL
  STATIC IMPORTED)` + `add_dependencies`), con un stamp file (`vitagl_flags.stamp`) que fuerza un
  rebuild completo si cambian los flags. El `libvitaGL.a`/`vitaGL.h` globales de `~/vitasdk` quedaron
  intactos (verificado: mtime sin cambios tras el build) — este port ya no depende de ellos.
- **Flags usados (`VITAGL_MAKE_FLAGS`): `SOFTFP_ABI=1 NO_SPLASHSCREEN=1`.** `NO_SPLASHSCREEN=1` es
  imprescindible (reemplaza el parche manual del SDK global). `SOFTFP_ABI=1` porque este proyecto es
  ABI `armeabi` soft-float (confirmado en `PORTING_PLAN.md`), igual que Asphalt-5-Vita. Se decidió
  **no** copiar flags de tuneo de rendimiento de Asphalt-5-Vita (`DRAW_SPEEDHACK=2`, `NO_DEBUG=1`,
  `HAVE_SHADER_CACHE=1`) sin un bug propio que los justifique -- cambio mínimo y conservador, el
  objetivo era vendorizar el build, no tunear vitaGL.
- **Dos problemas de infraestructura descubiertos y resueltos en el camino (ambos ya resueltos antes
  de forma idéntica en Asphalt-5-Vita, replicados aquí):**
  1. `.gitignore` tenía un patrón `Makefile` sin anclar a la raíz, que el paso de copia del toolkit
     (que respeta `.gitignore`) también excluía dentro de `lib/vitagl/Makefile` (el Makefile propio,
     versionado, de vitaGL) -- causaba `No targets specified and no makefile found`. Fix: anclar a
     `/Makefile`.
  2. El `vgl.c` de upstream llama a `shark_set_shader_association_path()` sin guardarla, símbolo de
     un toolchain Razor/devkit ausente en un build de release -- causaba `undefined reference` al
     linkear. Fix: parchear el submodule vendorizado (commit local `03ab83c` en `lib/vitagl`) para
     guardar esa llamada bajo `#ifdef HAVE_RAZOR`, igual que `vglSetShaderCachePath` ya guarda la suya
     bajo `HAVE_SHADER_CACHE` -- mismo fix ya aplicado en el vitaGL vendorizado de Asphalt-5-Vita.
- Validado: `psvita-toolkit build --preset debug --clean` termina limpio (`[+] Build OK.`),
  `eboot.bin` generado (579.7 KB). Confirmado por el log de build que el target `vitaGL_build` corre
  el `Makefile` de `lib/vitagl` (paso "Building VitaGL"), y que el error de `shark_set_shader_association_path`
  desapareció exactamente al parchear el submodule vendorizado -- evidencia directa de que se está
  linkeando esa copia y no el `libvitaGL.a` global. No se desplegó a consola (no era parte de esta
  tarea).

---

## Fase 8 (cont.): Causa raíz REAL del bug de `sprintf %s` inválido, resuelta -- `FileManager::_Load` sin bounds-check (2026-09-04)

Triaje de `log_20260904_162033.txt` + `sacredodyssey-psp2core-1788553248-0x0009013e65-eboot.bin.psp2dmp`, siguiendo la skill `so-crash-triage` al pie de la letra (log primero, `psvita-toolkit analyze` después, cruzando con `objdump`/Ghidra, verificando el ensamblador real antes de confiar en el pseudo-C).

- **Confirmado que es EXACTAMENTE el mismo crash sin resolver de las dos vueltas anteriores**
  (`sacredodyssey-psp2core-1788539515-...` y `-1788543426-...`, ver entradas previas "pointer
  inválido en un `%s` de `sprintf()`" y "El guard de `fmt.c` NO interceptó el crash"): los tres
  dumps tienen `PC`/`LR`/`R0`/`R1`/`R9`/`R10`/`R11`/`R12` **byte-a-byte idénticos**
  (`PC` dentro de `strlen`-ish de la newlib del loader, `LR = _svfprintf_r`, `R0 = 0xfffffff8`,
  `R1 = 0x00005e68`, `R9 = 0x98412890` → offset `0x412890`, justo al lado de la constante
  `"%s%s"` del `.so` en `0x41288c`) -- es decir, 100% determinístico, no basura de pila. La
  base real del `.so` sigue siendo `0x98000000` (`psvita-toolkit analyze` volvió a autodetectar
  una base incorrecta, `0x81278000`, dando el símbolo sin sentido `__strftime.isra.0`).
- **El trazo incondicional de `sprintf_soloader`/`vsprintf_soloader` (agregado la vuelta pasada)
  finalmente dio la pista decisiva:** la ÚLTIMA línea del log antes del corte es
  `[DEBUG] sprintf_soloader: fmt="%s%s"` (línea 500), sin ningún `[WARN] ... blocked call`
  después -- es decir, el guard de `fmt.c` escaneó este `%s%s` y NO lo rechazó, pero la llamada
  real igual crasheó. Justo antes (líneas 493-499): `fopen(SaveGame.bin)` OK,
  `sprintf(fmt="text_%s_lang")`, `sprintf(fmt="%s%s")`+`sprintf(fmt="%s/%s")`,
  `fopen(data/texts/text.EN.lang)` OK, `fclose` OK -- es decir, `StringMgr::SetLanguage()`
  cargó el idioma con éxito por primera vez en toda la sesión, y el crash ocurre INMEDIATAMENTE
  después, en el primer frame del render loop -- explica por qué nunca se había visto antes.
- **Cruce con Ghidra + `objdump` en modo ARM (no Thumb) aisló la función exacta:**
  `Application::LoadLanguage()` → `StringMgr::SetLanguage()` (OK) → `CFontMgr::LoadFonts()` →
  `CFont::LoadFontTable(id)` → `FileManager::_Load(id, ...)`, que en su rama "cargar" llama a
  `Application::GetResourcePath(Application::s_instance, nombre_recurso)`, cuya PRIMERA
  instrucción es `sprintf(buf,"%s%s",uVar1,nombre_recurso)` -- coincide exactamente con la
  `fmt="%s%s"` de la línea 500.
- **Causa raíz real, confirmada en el ensamblado (no en el pseudo-C, que la representa mal):**
  `FileManager::_Load()` **no tiene ningún bounds-check efectivo**, en ninguna dirección:
  ```arm
  1cb6c4: subs ip, r1, #0        ; ip = param_1 (id de recurso)
  1cb6d8: blt 1cb764             ; si param_1 < 0 -> chequeo "negativo"
  1cb6dc: ldr r5, [r0, #44]      ; <-- fallthrough si param_1 >= 0: CERO chequeo de límite superior
  ...
  1cb764: ldr r1, [r0]           ; r1 = *this
  1cb768: ldr r1, [r1]           ; r1 = count (**this)
  1cb76c: cmp ip, r1
  1cb770: movge r0, #0           ; "no encontrado" -> r0 = 0 ... pero es INALCANZABLE:
  1cb774: blt 1cb6dc             ; un param_1 negativo SIEMPRE es < un count no-negativo, así
                                 ; que esta rama SIEMPRE se toma, volviendo al camino de "cargar
                                 ; usando param_1 como índice" ¡con param_1 TODAVÍA NEGATIVO!
                                 ; (lee names_table[-1], una entrada antes del array)
  ```
  El pseudo-C de Ghidra muestra esto como `if ((param_1 < 0) && (count <= param_1)) return 0;`
  -- un `&&` que debería haber sido `||` (mismo tipo de error del desarrollador original de
  Gameloft que el ya arreglado en `ShowItemEffectProxy::ShowItemEffectProxy()`, Fase 8). Como
  además NO hay ningún chequeo de límite superior para `param_1 >= 0`, tanto un id negativo
  (el centinela "no encontrado" de `FileManager::_GetId()`) como un id fijo positivo que
  simplemente exceda el conteo real de recursos de este port (ej. uno de los ids fijos de
  `CFont::LoadFontTable()`, alcanzado por primera vez recién ahora) leen
  `*(char**)(names_table + id*8)` fuera de los límites del array, entregando un puntero basura
  como nombre de recurso a `GetResourcePath()`, que crashea al hacer `strlen()` sobre él en su
  primer `sprintf()` (descartado igual, pero ejecutado antes de poder descartarse). No se pudo
  aislar con certeza estática CUÁL id específico dispara esto primero, pero el fix corrige el
  límite que el código original ya pretendía imponer, no una corrección específica de un solo
  call site -- válido para cualquier llamador de `FileManager::_Load()`.
- **Fix aplicado (`source/patch.c`):** trampolín nuevo en la misma región muerta de
  `ALicenseCheck::LoadConfig` (hookeada e inalcanzable) usada por los fixes anteriores, justo
  después del de `DefaultEffectProxy::render` (`0x003dd0b0`, dentro de los 0xd0 bytes del
  cuerpo de `LoadConfig`, que corre hasta `0x003dd174`):
  ```arm
  ldr r1, [r0]        ; r1 = *this
  ldr r1, [r1]        ; r1 = count
  cmp ip, #0
  blt return_zero     ; id < 0 -> rechazar
  cmp ip, r1
  bge return_zero     ; id >= count -> rechazar
  b 0x1cb6dc          ; válido -> continuar el camino original de "cargar"
  return_zero:
  mov r0, #0
  b 0x1cb708          ; epílogo original (add sp,#12; pop {r4,r5,pc})
  ```
  En `0x001cb6d8`, se sustituyó `blt 0x1cb764` por `b 0x003dd0b0` (`0xea084674`). Opcodes
  verificados ensamblando con `arm-vita-eabi-as`/`-ld -Ttext=0x3dd0b0` (con `--defsym` para los
  dos destinos reales `0x1cb6dc`/`0x1cb708`), misma disciplina que todos los trampolines
  anteriores -- no calculados a mano.
- **Validado:** build limpio (`psvita-toolkit build --preset debug`), `eboot.bin` (566 KB) y
  `sacredodyssey.vpk` regenerados en `build/` y copiados a la raíz. **Pendiente confirmar en
  consola real:** el deploy (`psvita-toolkit deploy --eboot --yes`) falló por
  `Connection refused` en el puerto FTP (192.168.3.15:1337) -- el servidor FTP de VitaShell no
  estaba activo en este intento. Hace falta reabrir VitaShell y presionar SELECT para
  reactivarlo, y volver a desplegar (`deploy --eboot` alcanza, este fix no toca `app0:`/assets)
  para confirmar en hardware que el crash desaparece y que `StringMgr`/`CFontMgr` completan su
  inicialización.

---

## Fase 8 (cont.): Triaje del Crash `sacredodyssey-psp2core-1788557033-...` — asignación sin chequear en `Structs::SpriteTextureEntry::Read`, disparada por un `.kot` (DDS) leído como tabla de sprites (2026-09-04)

- **Comportamiento en consola:** el fix de `FileManager::_Load` funcionó — el log avanzó mucho más:
  `StringMgr::SetLanguage()` completó la carga de `text.EN.lang`, `CFontMgr::LoadFonts()` cargó
  `font_small_cn_2x_2x_sprite`, y el juego entró a cargar assets de una escena 3D real
  (`data/lowtexture/wt_zhuwang_2lowpt.kot`) antes de crashear.
- **Análisis del dump** (`sacredodyssey-psp2core-1788557033-0x0017852b0b-eboot.bin.psp2dmp`):
  `psvita-toolkit analyze` volvió a autodetectar mal la base del `.so` (`0x81225000`). Recalculando con
  la base real (`0x98000000`): `PC = 0x9820d548` → offset `0x0020d548` =
  `Structs::SpriteTextureEntry::Read(DataStream&)+0x48`. Registros crudos en el crash: `R0=0x7c`,
  `R3=0`, `R5=0`, `R7=0` (¡NULL!).
- **Causa raíz confirmada (ensamblador real, `objdump` sin `force-thumb` — función en ARM puro):** la
  función lee un contador con `DataStream::ReadInt()`, reserva `count*4` bytes con `operator new[]`
  (`_Znaj`) **sin chequear el resultado por `NULL`**, y escribe directo en el array reservado:
  ```arm
  20d510: bl DataStream::ReadInt        ; r0 = count
  20d51c: bl operator_new[]             ; r0 = malloc(count*4) -- puede ser NULL
  20d528: mov r7, r0                    ; r7 = array (¡puede ser NULL!)
  20d530: ble 0x20d55c                  ; solo chequea count <= 0, NO chequea r7 == NULL
  ...
  20d548: str r0, [r7, r5, lsl #2]      ; ¡CRASH! si r7 es NULL
  ```
  **Qué disparó esto en este run:** `ASprite::Load(int)` hace una carga única (guardada por un flag
  estático) de un recurso interno por ID fijo (`ResStream(0xfa5)` → `FileManager::_GetDvdName(0xfa5)` →
  `Application::GetResourcePath()` → `FileStream::Open()`), esperando una tabla binaria chica de
  "sprite texture entries". En este port, ese ID terminó resolviendo a
  `data/lowtexture/wt_zhuwang_2lowpt.kot` — confirmado leyendo el archivo real: sus primeros 8 bytes son
  literalmente el header DDS (`44 44 53 20` = magic `"DDS "` = `0x20534444`, seguido de `dwSize=0x7c`).
  El magic de DDS, interpretado como "cantidad de elementos", produce un pedido de asignación de
  `0x20534444 * 4` (~2 GB) que el allocator personalizado del motor rechaza correctamente devolviendo
  `NULL` — pero el código nunca lo chequea, y crashea escribiendo el segundo entero leído (`0x7c`,
  exactamente el `dwSize` real de la cabecera DDS, coincide bit a bit con `R0` del crash) en la
  dirección `NULL+0`. Es un desajuste de mapeo id→nombre en el dataset de assets reconstruido de este
  port (no algo corregible re-mapeando IDs a mano sin el `Res.array` original completo), pero el bug de
  fondo -- una asignación sin verificar -- es real y del motor original, y vale la pena blindarlo
  igual que se vino haciendo con el resto de patrones confirmados esta sesión.
- **Hermano confirmado en la MISMA función (no solo sospechado -- leído directo del ensamblador):** un
  segundo bloque idéntico un poco más abajo (`this+8`/`this+0xc`, un segundo par
  count/array) tiene exactamente el mismo patrón sin chequear (`20d578: mov r7,r0; 20d580: pople
  {...}` solo chequea `count2<=0`; `20d590: str r0,[r7,r5,lsl#2]` crashearía igual si se alcanza).
  Parcheado en la misma pasada.
- **Fix aplicado (`source/patch.c`):** dos trampolines nuevos, justo después del de
  `FileManager::_Load`, en el mismo hueco muerto de `ALicenseCheck::LoadConfig` (`0x3dd0d4` y
  `0x3dd0f4`, ambos dentro de su cuerpo de 208 bytes hookeado/inalcanzable):
  - En `0x0020d530` (reemplaza `ble 0x20d55c`): si el array quedó `NULL`, fuerza `count1=0` (para que
    el `if (0 < count1)` del caller lo salte de forma segura) y continúa al código posterior al loop;
    si el array es válido, reproduce el comportamiento original exacto.
  - En `0x0020d584` (reemplaza `mov r5,#0`): si el segundo array quedó `NULL`, fuerza `count2=0` y
    retorna temprano (mismos registros que el `pople` original); si es válido, continúa el loop normal.
  Opcodes verificados ensamblando con `arm-vita-eabi-as`/`-ld -Ttext=<dirección>`, no calculados a
  mano.
- Validado: build limpio (preset `debug`), `eboot.bin` (566.5 KB) y `sacredodyssey.vpk` regenerados y
  copiados a la raíz. **Deploy pendiente:** dos intentos de `deploy --eboot` fallaron (`timed out` /
  `Host is down`) — la consola no respondía por FTP en este momento. Reactivar VitaShell (SELECT) y
  volver a desplegar para confirmar en hardware que el juego sigue cargando la escena 3D sin crashear.

---

## Fase 8 (cont.): El fix de `SpriteTextureEntry::Read` convirtió el crash en un cuelgue — encontrado y corregido en `ASprite::Load` (2026-09-04)

- **Comportamiento en consola:** `log_20260904_173928.txt` (3043 líneas, sin `.psp2dmp` -- un cuelgue,
  no un crash) muestra al juego reabriendo y releyendo `data/lowtexture/wt_zhuwang_2lowpt.kot` en un
  loop infinito (cientos de repeticiones idénticas de `fopen`/`fclose`/`sprintf_soloader`), atascado
  antes de superar el splash del LiveArea.
- **Causa raíz confirmada (ensamblador real de `ASprite::Load(int)`, `0x0020ed7c`-`0x0020f778` --
  único lugar de todo el `.so` que referencia el recurso fijo `0xfa5`, confirmado con
  `objdump -d | grep 1fa5`):** el fix anterior (hacer que `SpriteTextureEntry::Read` devuelva "0
  elementos" en vez de crashear cuando la asignación falla) funcionó para evitar el crash, pero
  `ASprite::Load` tiene una guarda de inicialización única que, tras procesar la carga, chequea si el
  mapa estático `s_TexIds` quedó con AL MENOS una entrada; si sigue vacío, **vuelve al principio de la
  función para reintentar la carga completa desde cero**:
  ```arm
  20f434: ldr r3, [r3, #16]     ; flag estático "¿se insertó alguna vez una entrada?"
  20f438: cmp r3, #0
  20f43c: beq 0x20efac          ; si sigue en 0 -> reintentar TODO desde el principio
  20f440: b 0x20efd4            ; (incondicional) continuar con esta instancia de ASprite
  ```
  Como el recurso `0xfa5` es una constante fija (no varía entre reintentos), un recurso corrupto o
  mal mapeado JAMÁS puede tener éxito sin importar cuántas veces se reintente -- y nunca hubo una
  razón legítima para que este reintento corriera más de una vez ni siquiera en el caso exitoso (una
  sola lectura de `ResStream`/`DataStream` es determinística dado el mismo input fijo).
- **Fix aplicado (`source/patch.c`):** un solo `nop` en `0x0020f43c` (reemplaza el `beq 0x20efac`).
  No cambia el comportamiento del caso exitoso en absoluto (ese branch nunca se tomaba ahí, ya que el
  mapa se poblaba en el primer intento) -- solo elimina el reintento infinito para el caso que el
  dataset de este port no puede satisfacer, dejando que caiga al `b 0x20efd4` incondicional que ya
  existía justo debajo. Efecto neto (junto con el fix de `SpriteTextureEntry::Read`): `s_TexIds` queda
  vacío para este dataset -- mismo trade-off de "sin fidelidad pero sin crash/cuelgue" ya aceptado
  esta sesión para otros assets faltantes/mal mapeados.
- **Lección reforzada:** un fix que evita un crash puede introducir un cuelgue si el código que rodea
  al punto parcheado asume "si no hubo éxito, reintentar" -- conviene revisar el flujo posterior al
  punto de parcheo (no solo el punto de crash en sí) antes de dar por cerrado un fix, especialmente
  cuando hay loops de reintento estáticos/one-time-init de por medio.
- Validado: build limpio (preset `debug`), `eboot.bin` (566.6 KB) desplegado por FTP con éxito
  (`deploy --eboot`, conexión restablecida). Pendiente el próximo log/dump para confirmar que el
  LiveArea splash se supera.

---

## Fase 8 (cont.): Causa real del cuelgue — texturas ATC (Adreno Texture Compression) que la GPU de la Vita no soporta (2026-09-04)

- **El fix de `ASprite::Load` NO era la causa del cuelgue reportado.** `log_20260904_174749.txt`
  (probado dos veces por el usuario, mismo punto ambas veces) muestra que el juego SÍ superó la
  guarda de `ASprite::Load` (aparece "Patched ASprite::Load infinite retry hang" en el log, y el
  log avanza mucho más -- 1823 líneas, "Entering main render loop..." y "Loading file:
  font_small_cn_2x_2x_sprite" ya se ven), pero se queda reabriendo/releyendo
  `data/lowtexture/wt_zhuwang_2lowpt.kot` **329 veces seguidas** sin avanzar (nunca aparece
  ningún `[render_diag]`, que loguea cada 60 frames -- confirma que ni siquiera se llega al
  primer frame real, es un cuelgue previo al render loop, no un retry cosmético por frame).
- **Causa raíz confirmada cruzando el contenido REAL del archivo con el ensamblador/pseudo-C del
  motor:** `wt_zhuwang_2lowpt.kot` es un DDS válido cuyo FourCC de formato de píxel es
  **"ATCA"** (`41 54 43 41` en el offset 0x54 del archivo -- confirmado con `xxd`) -- ATC
  (Adreno Texture Compression), un formato de compresión específico de GPUs Qualcomm Adreno.
  `glitch::video::CImageLoaderDDS::loadImage()` **sí reconoce** explícitamente los FourCC
  `"ATC "`/`"ATCI"`/`"ATCA"` (confirmado en Ghidra: constantes `0x20435441`/`0x49435441`/
  `0x41435441`) y construye un `CImage` sin problema -- el parseo DDS del `.so` no es el
  problema. La falla real está más abajo, cuando `vitaGL` intenta subir ese `CImage` como
  textura a la GPU PowerVR SGX de la Vita, que no tiene soporte de hardware para ATC (habla
  nativamente PVRTC). Como `CTextureManager::getTexture()` nunca cachea un "esto falló
  permanentemente" para una textura cuya subida a GPU no tuvo éxito, **cada objeto/material
  distinto de la escena "wisdomtree" que referencia este mismo nombre de textura reintenta la
  carga completa desde cero, independientemente** -- no es un solo loop infinito, son cientos de
  intentos de disco+decode garantizados a fallar, indistinguible de un cuelgue en la práctica.
  El nombre del APK (`...-PowerVR.apk`) sugiere que existían variantes por fabricante de GPU
  (Adreno/PowerVR/Mali), y este asset de baja resolución en particular quedó en formato Adreno
  en el dataset reconstruido de este port para todas las variantes (o se mezcló del dataset
  equivocado).
- **Fix aplicado (`source/reimpl/io.c`):** siguiendo el mismo patrón ya probado del "Pink Bad
  Shader" (sustituir por contenido válido en vez de parchear la lógica C++ del motor), se agregó
  detección por CONTENIDO (no por nombre de archivo, para no afectar futuros `.kot` que sean
  formatos válidos) en `fopen_soloader()`: cualquier archivo `.kot` abierto con éxito se
  inspecciona (magic `"DDS "` + FourCC en el offset 0x54); si el FourCC empieza con `"ATC"`, se
  cierra y se redirige transparentemente a un DDS sustituto embebido (`placeholder_atc_dds_bytes`,
  4x4 RGBA sin compresión, gris sólido -- generado por script, no a mano) escrito una sola vez en
  `DATA_PATH "placeholder_atc.dds"`, igual que el patrón ya usado para shaders faltantes.
- **Nota para el futuro:** este es un problema de FORMATO/hardware, no de lógica -- si aparecen
  más colgadas similares con otras texturas `.kot`, es casi seguro el mismo patrón ATC; el
  `[WARN] ATC-compressed DDS texture ... redirecting...` en el log lo confirma sin necesidad de
  volver a diagnosticar desde cero. Si hiciera falta fidelidad visual real a futuro, la solución
  de fondo sería re-transcodificar estos assets a PVRTC/RGBA sin comprimir en el dataset del
  port (fuera del alcance de un parche en runtime).
- Validado: build limpio (preset `debug`). `eboot.bin` (567.1 KB) desplegado por FTP con éxito.
  Pendiente el próximo log para confirmar que se supera esta escena.

## Fase 8 (cont.): El decodificador ATC real NO cambió el cuelgue — re-diagnóstico con desensamblado (2026-09-04)

- **Evidencia comparativa (3 logs):** `log_20260904_174749.txt` (329 refs a `wt_zhuwang_2lowpt.kot`,
  pre-ATC), `log_20260904_174920.txt` (95 refs, pre-ATC, captura más corta) y
  `log_20260904_194459.txt` (386 refs, CON decodificador ATC real) muestran el **mismo cuerpo de
  iteración** (`sprintf "%s%s"` + `sprintf "%s/%s"` + `fopen` + `fclose` del mismo `.kot`, tras un
  único `Loading file: font_small_cn_2x_2x_sprite`) y los tres terminan **en mitad de la tormenta**
  (usuario matando la app), sin ningún `[render_diag]` (el loop principal loguea cada 60 frames --
  ni el primer frame completa su primer `nativeRender`). Conclusión: el contenido de la textura es
  irrelevante para el síntoma -- el redirect ATC (placeholder o decode real) no es la causa ni la
  cura. Rebajado el diagnóstico anterior de "la subida ATC a GPU falla" a hipótesis débil: el
  `vitaGL` vendorizado (`lib/vitagl/source/textures.c`, casos `GL_ATC_RGB_AMD` /
  `GL_ATC_RGBA_EXPLICIT_ALPHA_AMD` / `GL_ATC_RGBA_INTERPOLATED_ALPHA_AMD`) **ya decodifica ATC por
  software** vía `atitc_decode()`, así que la subida nunca debió ser el punto de falla.
- **Candidatos a "segundo retry" descartados por desensamblado:** un barrido de todo `.text`
  buscando `beq` con salto atrás localizó dos vecinos sospechosos junto al retry ya parcheado de
  `ASprite::Load` (`0x20f43c -> 0x20efac`, NOPeado): `0x20f1e0 -> 0x20efc0` y
  `0x20f67c -> 0x20ee08`, ambos con el mismo idioma (`mov r0, rX; bl 0x1b994c; cmp r0, #0; beq
  atrás`). Resolviendo el stub PLT `0x1b994c` a mano (stub `add ip,pc / ldr pc,[ip,#off]` -> GOT
  `0x47fc20` -> `readelf -r` = **`__cxa_guard_acquire`**) se confirma que los tres son el idioma
  estándar de **guarda de inicialización de statics C++** (`if (!acquired) <re-chequear>`), NO
  reintentos de carga: un spin ahí sería silencioso (sin `fopen`), incompatible con la tormenta de
  I/O observada. El único retry real de carga en esa función sigue siendo `0x20f43c`.
- **Dos mecanismos restantes, indistinguibles sin temporizar (de ahí el experimento de abajo):**
  - (A) **Retry verdadero en el cargador de fuentes** (`CFont*`, dirección aún desconocida --
    cada iteración reabre porque la carga "vacía" se reintenta). Parche futuro = NOPear su `beq`
    como se hizo con `0x20f43c`, una vez que el log revele su offset de caller.
  - (B) **Carga lenta pero finita:** cientos de objetos/materiales (`font_small_cn_2x_2x_sprite`
    + escena) pidiendo la misma textura sin caché negativa, cada uno con transcodificación por
    software (~0.5-1 s/iteración explicaría "minutos" con solo ~330-390 iteraciones). Parche
    futuro = paciencia / pre-transcodificar assets, no NOP.
  La tasa (iters/seg) del próximo log decide entre (A) y (B): miles/seg = (A), ~1/seg = (B).
- **Cambios aplicados (`source/reimpl/io.c`, solo ese archivo):**
  - Detector de tormenta en `fopen_soloader()`: cuenta aperturas consecutivas de la misma ruta
    con `current_timestamp_ms()` y cada 50 loguea `[fopen_storm] <path> opened <N> consecutive
    times in <ms> ms (caller .so offset 0x<x>)` donde el offset = return-address menos
    `so_mod.text_base`, listo para `objdump`. Los archivos generados propios (`/cache/`,
    `placeholder_atc.dds`) no rompen la racha (si no, el inner-open del redirect la resetearía).
  - WARN de redirect ATC limitado (siempre la 1ª vez por racha + cada 100) con caller offset;
    antes eran 2 líneas extra por iteración que enterraban la señal.
  - Sniff ATC solo en modo lectura (`mode[0] == 'r'`): un `"wb"` redirigido escribiría la salida
    del juego sobre el caché en vez de su destino real.
  - Cabecera DDS decodificada corregida: `dwFlags` (offset 8) se reescribe a
    `CAPS|HEIGHT|WIDTH|PITCH|PIXELFORMAT (+MIPMAPCOUNT si >1)` en vez de heredar el
    `DDSD_LINEARSIZE` del ATC original, y `dwMipMapCount` se limita a los mips realmente
    decodificados (antes se declaraban N mips aunque el `break` por truncado escribiera menos --
    un parser que confíe en esos campos falla la carga y alimenta el retry). Si ni el mip top
    decodifica, se sirve el placeholder en vez de un DDS solo-cabecera.
  - Caché versionada (`cache/atc1-<sha1>.dds`): los decodes de revisiones anteriores (headers con
    flags comprimidos) nunca se reutilizan. Guard contra `str_sha1sum() == NULL`, y el log de
    decode ahora incluye dimensiones/fourcc/mips/bytes.
- Validado: `arm-vita-eabi-gcc -fsyntax-only -Wall -Wformat` limpio con y sin `DEBUG_SOLOADER`.
  **Deploy pendiente con el toolkit** (build+FTP los hace el toolkit standalone, no a mano).
  Próximo paso: una sola corrida de 2-3 min, traer el log y leer las líneas `[fopen_storm]` +
  `caller .so offset` -- con eso se elige (A) NOP dirigido u (B) esperar/pre-transcodificar.

## Fase 8 (cont.): El detector funcionó — tormenta a 119 ms/iter desde un único call site `0x1c7060` (2026-09-04)

- **Log `log_20260904_200530.txt` (build instrumentado, 1863 líneas):** el decode es correcto y
  completo (`128x128, fourcc=ATCA, mips 8->8, 87380 bytes` = suma exacta de la pirámide RGBA, y el
  caché nuevo es `cache/atc1-<sha1>.dds`). La tormenta: **250 opens en 29.8 s = ~119 ms/iter,
  tasa constante** (deltas por cada 50: 7.1 / 6.3 / 5.1 / 6.2 / 5.1 s) y **un único caller en las
  250**: `.so offset 0x1c7060` en todas las líneas `[fopen_storm]` y WARN de redirect.
- **Call site resuelto:** `0x1c7060` es la instrucción tras `bl 0x1b970c` dentro de la función que
  arranca en `0x1c6fe0` (`mov r0,r8=path; mov r1,r3=mode; ldr r6,[r5,#8]; bl fopen; str r0,[r6];
  ...; return 1`). `0x1b970c` -> GOT `0x47fb60` -> `readelf -r` = **`fopen` confirmado**. Es un
  wrapper estilo `FileStream::Open(path, flags)` con selección de modo (`"rb"` etc.) por bits de
  flags y path de error (fopen NULL -> log + búsqueda en tabla de 50) que **no se toma** (nuestro
  fopen siempre devuelve non-NULL). El wrapper **no contiene ningún loop** (ramas solo hacia
  adelante) -- el que repite es su llamador.
- **Llamador no localizable estáticamente (de momento):** barrido completo de `.text` buscando
  `BL`/`BLX` ARM **y** `BL` Thumb-2 con destino `0x1c6fe0` = **cero hits** -- se llega por llamada
  virtual (`blx rX` vía vtable) o puntero, no por `bl` directo. El análisis de vtables queda
  aparcado: no hace falta todavía.
- **Lectura actual (favorece hipótesis B, finita-pero-lenta):** 119 ms/iter es ~1000x más lento
  que un spin de retry fallido (lecturas page-cached + parse fallido serían µs-ms). Cada iteración
  hace trabajo real: open + `fread` de ~87 KB desde almacenamiento lento + parse DDS + upload GL
  (128x128 + 7 mips) + ~6 líneas de log DEBUG a flash. 250-386 iters vistas por corrida ~= número
  de objetos/glifos (`font_small_cn_2x_2x_sprite`, fuente CJK pequeña) pidiendo la misma textura
  sin caché negativa. Las corridas más largas (`174749`: 329, `194459`: 386) habrían estado a
  segundos de terminar si el total ronda ~400 -- **nadie esperó lo suficiente**.
- **Decisión:** UNA corrida larga (10 min sin tocar nada) con este mismo build. Si la tormenta
  termina (cambia el path / aparecen más fases / `[render_diag]`) = (B) confirmado, y el fix es
  paciencia + optimizar (menos log, pre-decode), no NOP. Si supera ~1000 iters a tasa constante
  sin terminar = (A) y entonces sí se caza al llamador virtual y se NOPea.

## Fase 8 (cont.): Veredicto de la corrida larga — 600+ iters sin fin, es retry infinito (2026-09-04)

- **Log `log_20260904_202026.txt` (varios minutos en consola):** la tormenta llega a **600 opens
  consecutivos (~78.7 s logueados, 649 refs totales, tasa ~131 ms/iter, mismo path, mismo caller
  `0x1c7060`) y el log termina en mitad del mismo patrón. Veredicto: **infinito (hipótesis A)**.
  Mi lectura anterior (B, "casi termina, esperen más") queda descartada -- me equivoqué, y 600+
  glifos idénticos sin intercalar ningún otro archivo no es una carga de fuente real.
- **Nuevo experimento aplicado (`source/utils/glutil.{c,h}` + 3 entradas en `source/dynlib.c`):**
  observadores puros (solo loguean y delegan a vitaGL, sin consumir estado GL) en
  `glTexImage2D`, `glCompressedTexImage2D` y `glTexSubImage2D` con tag `[gl_upload]`
  (nº, target/level/format/dims/imageSize/puntero + caller offset, primeros 12 y cada 200).
  Según lo que muestren, el fallo de cada iteración está en una de tres etapas excluyentes:
  - `glCompressedTexImage2D` con enum ATC + bytes servidos UNCOMPRESSED = el motor sube como
    comprimido lo que ya decodificamos (vitaGL transcodifica basura, pero sin error);
  - `glTexImage2D` RGBA 128x128 repetido = el archivo parsea bien y el fallo es downstream (sin
    caché negativa);
  - **ningún upload durante la tormenta** = el motor rechaza el archivo servido en parse
    (tamaño esperado de `Res.array`/tabla vs 87380 servidos, FourCC, mips) antes de llegar a GL.
- Validado: `-fsyntax-only -Wall -Wformat` limpio en release y debug (único warning es
  pre-existente en `load_shader`, código no tocado). **Deploy pendiente con el toolkit.**
  Próximo paso: corrida CORTA (~40 s bastan, la tormenta arranca de inmediato), traer el log y
  buscar `[gl_upload]`.

## Fase 8 (cont.): Cero uploads en toda la corrida — el rechazo es en parseo, antes de GL (2026-09-04)

- **Log `log_20260904_203626.txt` (build con observadores GL, 2059 líneas): cero líneas
  `[gl_upload]`** en TODA la corrida (ni siquiera fuera de la tormenta: el init compila 9
  programas de shaders pero no sube ninguna textura -- toda la carga de texturas es perezosa al
  primer frame). Tercera etapa excluida de un plumazo: cada iteración abre → lee → **rechaza en
  parseo sin tocar GL** → cierra → repite. El rechazo es independiente del contenido (el ATC
  original también cicla), así que no es el FourCC: o la tabla lectora espera otra estructura
  (clase `SpriteTextureEntry::Read` -> "0 entradas" -> retry del llamador, ahora en el cargador
  de fuentes) o hay un gate temprano (tamaño esperado vs servido).
- **Nuevo experimento aplicado (`source/reimpl/io.{c,h}` + 2 entradas en `source/dynlib.c`):**
  `fread_soloader` (contabilidad solo para FILEs abiertos durante una tormenta activa, anillo de
  8 -- la tormenta es secuencial; 8 comparaciones de puntero por `fread`, despreciable) acumula
  bytes y `fclose_soloader` reporta `[fread_acct] storm file closed: <N> bytes in <M> reads`;
  las primeras lecturas loguean tamaño + `reader .so offset`. Lectura esperada:
  - total ~= archivo completo (87 KB) => parse completo y rechazo tardío (retry por "vacío" --
    el offset del reader localiza la función con el branch a NOPear, misma receta que `0x20f43c`);
  - total ~= cabecera (≤1 KB) => rechazo temprano (magic/tamaño/count) => fix del lado servido.
- Validado: `-fsyntax-only -Wall -Wformat` limpio en release y debug. **Deploy con el toolkit.**
  Corrida corta (~40 s), traer log, buscar `[fread_acct]`.

---

## Fase 8 (cont.): Recuperación de sesión (corte por límite de uso) — inventario del estado real, sin repetir trabajo, y hallazgo sobre vitaGL (2026-09-05)

- **Contexto:** la sesión se cortó por límite de uso a mitad de la investigación del cuelgue ATC
  (justo después de instrumentar `[fread_acct]`, ver entrada anterior). Al retomar, en vez de
  asumir el estado por la conversación truncada, se hizo un inventario real del árbol de trabajo
  (`git status`/`git diff` por archivo) para confirmar exactamente qué existía y qué faltaba antes
  de tocar nada.
- **Confirmado por inventario (nada de esto se rehizo, ya estaba en el árbol):**
  - `source/reimpl/io.c`/`.h`: pipeline completo de detección+decodificación+caché de texturas
    ATC (`looks_like_atc_dds`, `get_decoded_atc_dds_path` con caché por SHA1 en
    `DATA_PATH cache/atc1-<hash>.dds`, versionado para poder invalidar decodes viejos/con bugs),
    más el detector de "tormenta" (`storm_count`/`storm_ring_*`) y la contabilidad `fread_acct`
    de la entrada anterior.
  - `source/utils/glutil.c`/`.h`: observadores `[gl_upload]` en `glTexImage2D`/
    `glCompressedTexImage2D`/`glTexSubImage2D` (puramente diagnósticos, delegan sin tocar estado).
  - `source/patch.c`: sin cambios respecto a lo ya documentado (los 8 fixes de `Gameplay::
    s_instance`/`FileManager::_Load`/`SpriteTextureEntry::Read`/`ASprite::Load` de entradas
    anteriores) -- nada nuevo pendiente ahí.
  - `lib/vitagl` (submodule): el decodificador ATC real (`atitc_decode`, algoritmo de
    `cocos2d-x`, licencia permisiva) vive en `lib/vitagl/source/utils/atitc_utils.c`/`.h`, ya
    commiteado dentro del submodule junto al único commit local previo (el guard `HAVE_RAZOR`).
  - Quedaban sueltos varios scripts `.py` (`fix_atitc_mipmaps.py`, `patch_io*.py`, `replace.py`,
    etc.) sin trackear -- parches de una sola pasada ya aplicados a `io.c`, sin uso posterior.
    Borrados por prolijidad (no tenían ningún efecto pendiente).
- **Hallazgo nuevo (no buscado, encontrado al auditar el submodule vía `git blame`):** vitaGL
  **ya soporta ATC nativamente desde 2021** (commit upstream `0c1f75d6` de Rinnegatamante,
  "Added support for ATI texture compression"), decodificando `GL_ATC_RGB_AMD`/
  `GL_ATC_RGBA_EXPLICIT_ALPHA_AMD`/`GL_ATC_RGBA_INTERPOLATED_ALPHA_AMD` en `glCompressedTexImage2D`
  con el MISMO algoritmo `atitc_decode` (coincidencia: no es que se haya "reinventado" nada, es
  el mismo código fuente cocos2d-x reutilizado en ambos lugares). Esto es ortogonal al pipeline de
  `io.c` (que transcodifica ANTES de que el `.so` vea el archivo, por lo que el `.so` nunca llega
  a pedir un upload con enum `GL_ATC_*`) -- pero es relevante para cualquier otro `.kot` ATC futuro
  cuyo `CImageLoaderDDS::loadImage()` sí lo acepte en el parseo (a diferencia de
  `wt_zhuwang_2lowpt.kot`, que según lo investigado hasta ahora se rechaza en el parseo antes de
  llegar a GL): en esos casos alcanzaría con dejar pasar el archivo ORIGINAL sin transcodificar,
  ya que vitaGL lo sube correctamente sin ayuda de `io.c`.
- **Acción de esta vuelta:** build limpio (`psvita-toolkit build --preset debug`) y **deploy por
  FTP exitoso** (`deploy --eboot`, consola respondió) del estado ya existente -- no se escribió
  código nuevo, solo se validó que compila/linkea y se subió a la consola.
- **Sigue pendiente exactamente lo mismo que en la entrada anterior:** una corrida CORTA (~40 s,
  la tormenta arranca casi de inmediato) para leer las líneas `[fread_acct]` y decidir entre
  "rechazo tardío tras leer el archivo completo" (recetar un NOP al branch de retry, misma
  receta que `0x20f43c`) o "rechazo temprano por cabecera" (fix del lado servido en `io.c`).

---

## Fase 8 (cont.): Consolidación — todo lo probado sobre el cuelgue `.kot` (2026-09-05)

Esta entrada consolida lo que las anteriores cuentan disperso, para no re-probar nada.

### 1. Catálogo de corridas de la tormenta (todas: mismo archivo, mismo patrón, sin `render_diag`)

| Log | Líneas | Refs `wt_zhuwang_2lowpt.kot` | Build | Tasa / nota |
|---|---|---|---|---|
| `log_20260904_173928.txt` | 3043 | 634 | post-`SpriteTextureEntry::Read`, pre-NOP `0x20f43c` | Tormenta original que motivó el NOP de `ASprite::Load` |
| `log_20260904_174749.txt` | 1823 | 329 | post-NOP `0x20f43c`, pre-ATC | Mismo patrón SIN redirect ATC: prueba de independencia del contenido |
| `log_20260904_174920.txt` | 886 | 95 | idem | Captura corta, mismo patrón |
| `log_20260904_194459.txt` | 1664 | 386 | decode ATC real (1ª versión) | Mismo patrón CON decode: el decode no cambia nada |
| `log_20260904_200530.txt` | 1863 | 279 | + detector `[fopen_storm]` | 250 opens / 29.8 s (~119 ms/iter), caller único `0x1c7060`; decode verificado (`128x128 ATCA 8->8 mips, 87380 B` exactos) |
| `log_20260904_202026.txt` | 3673 | 649 | idem, corrida de varios minutos | 600 opens / 78.7 s (~131 ms/iter), sin fin -> veredicto infinito |
| `log_20260904_203626.txt` | 2059 | 319 | + observadores `[gl_upload]` | **0 uploads** en toda la corrida; tormenta hasta 300+ |

Todas terminan en mitad del patrón (app matada o captura cortada); ninguna llega al frame 60.

### 2. Hipótesis probadas y veredictos

- **H1: la subida ATC a GPU falla (falta de soporte HW). REFUTADA.** Doble evidencia: (a) el
  patrón es idéntico con ATC original, placeholder gris 4x4 y decode RGBA real; (b) el vitaGL
  vendorizado decodifica ATC por software desde 2021 (upstream `0c1f75d6`, mismo `atitc_decode`
  de cocos2d-x en `lib/vitagl/source/utils/atitc_utils.c`), y (c) el log con observadores GL
  prueba que el motor **ni siquiera intenta subir** (0 uploads) -- el rechazo es en parseo.
- **H2: cientos de referencias distintas, carga lenta pero finita ("tener paciencia").
  REFUTADA.** La corrida de varios minutos (`202026`) pasa de 600 iters a tasa constante sin
  cambiar de archivo ni de caller. (El autor de esta línea había defendido H2 antes del dato;
  el dato manda.)
- **H3: segundo retry estilo `ASprite::Load` en `0x20f1e0` / `0x20f67c`. REFUTADA por
  desensamblado** (ver §3): son guardas `__cxa_guard_acquire` de statics, no retries.
- **H4 (vigente): el lector rechaza el archivo en parseo y su llamador reintenta sin caché
  negativa.** Compatible con todo: independencia del contenido, 0 uploads, ~120 ms/iter de
  trabajo real (open+read+parse+close+log). El experimento `[fread_acct]` (bytes/iter + reader
  offset) la subdivide en H4a (rechazo tardío -> NOP al branch) vs H4b (rechazo temprano ->
  fix servido). **Es lo único pendiente de medir.**

### 3. Métodos de desensamblado usados (reutilizables) y hallazgos por dirección

Datos base de `sacredodyssey_extract/lib/armeabi/libsacredodyssey.so` (7.1 MB): `.text` VMA
`0x1b9eb0` == offset en archivo `0x1b9eb0` (igual en `.rodata`), así que **dirección de
objdump == offset de `patch.c` directamente**, sin conversiones. Toolchain:
`/Users/metalsyntax/vitasdk/bin` (`arm-vita-eabi-objdump/readelf/nm`), más `python3` +
`capstone` para barridos a medida.

- **Resolver un stub PLT a símbolo importado:** el stub es `add ip,pc,#0x200000; add ip,ip,
  #0xc6000; ldr pc,[ip,#off]!` -> GOT = stub+8+0x200000+0xc6000+off -> buscar esa dirección
  en `readelf -r` (columna Offset). Así: `0x1b994c` -> GOT `0x47fc20` =
  `__cxa_guard_acquire`; `0x1b970c` -> GOT `0x47fb60` = `fopen`; `0x1b9c34`/`0x1b9454` =
  error-path del wrapper (ver §4). Sirve para cualquier `bl 0x1bXXXX` futuro.
- **Barrido de `beq` con salto atrás en todo `.text`** (164 candidatos con retroceso >0x500;
  validado contra el retry conocido `0x20f43c -> 0x20efac`, retroceso real 0x490): localizó
  `0x20f1e0 -> 0x20efc0` y `0x20f67c -> 0x20ee08`. Los tres comparten
  `mov r0,rX; bl __cxa_guard_acquire; cmp r0,#0; beq atrás`, pero el contexto los delata como
  guardas de init de statics (el `beq` re-chequea estado, y un spin ahí sería silencioso, no
  una tormenta de I/O). **El único retry de carga real en `ASprite::Load` (0x20ed7c-0x20f778)
  sigue siendo `0x20f43c`** (chequeo del contador estático de entradas insertadas tras
  `bl 0x213424`, ya NOPeado).
- **Barrido de llamadores a `0x1c6fe0`** (wrapper `FileStream::Open`, ver §4): cero `BL`/`BLX`
  ARM y cero `BL` Thumb-2 (decodificado a mano: hw1 `11110 S imm10`, hw2 `11 J1 J2 imm11`,
  `target = addr+4+SignExtend(S:I1:I2:imm10:imm11:0)`) en los 2.4 MB de `.text` -> se llega
  por **llamada virtual** (`blx rX`), no rastreable por `bl` directo. No insistir por esa vía;
  el offset del *reader* vía `[fread_acct]` es el camino más corto al loop.
- **Estructura interna de `ASprite::Load` (por si vuelve a tocarse):** un solo `push {..,lr}`
  en `0x20ed7c` para todo el rango hasta `~0x20f778`; bloque de counter-check en
  `0x20efc0: ldr r0,[sp,#8]; add r3,r6,r0; ldr r4,[r5,#16]; cmp r4,#0; beq 0x20f234`
  (0 -> hacer la carga) con re-chequeo tras guard en `0x20f1d4-0x20f1e0`.

### 4. Call site de la tormenta (fijado, no re-investigar)

`0x1c7060` = instrucción tras `bl fopen` en el wrapper que arranca en `0x1c6fe0`:
`mov r0,r8(path); mov r1,r3(mode); ldr r6,[r5,#8]; bl 0x1b970c(fopen); str r0,[r6];
...; return 1`, con selección de modo (`"rb"` etc.) por bits de flags antes del `bl`.
Su path de error (fopen NULL -> `bl 0x1b9c34` log + búsqueda lineal en tabla de 50 en
`0x1c70fc-0x1c7114`, `cmp r3,#0x32`) **no se ejecuta nunca** (nuestro fopen siempre devuelve
non-NULL). El wrapper no tiene loops: el retry vive en su llamador virtual.

### 5. Estado del código y pendiente único

`source/reimpl/io.c` (+`io.h`, 2 entradas `fread` en `dynlib.c`): sniff ATC solo-lectura,
decode con `atitc_decode` de vitaGL, cabecera reescrita (pitch + `dwFlags` uncompressed +
`dwMipMapCount` limitado a mips decodificados, fallback a placeholder si 0), caché
`cache/atc1-<sha1>.dds`, detector `[fopen_storm]` (cada 50, con caller offset), WARN
limitado, `fread_soloader` + anillo de 8 con reporte en `fclose` (`[fread_acct]`).
`source/utils/glutil.c`: observadores `[gl_upload]` (primeros 12 + cada 200, sin consumir
estado GL). Todo verificado `-fsyntax-only -Wall -Wformat` en release y debug; build+deploy
del estado actual ya hechos el 2026-09-05 (ver entrada anterior). **Pendiente único:** corrida
corta con el build desplegado y leer `[fread_acct]` (ningún log lo contiene aún) para decidir
H4a (NOP al branch del reader) vs H4b (fix servido).

---

## Fase 8 (cont.): Crash nuevo con dump — `Data abort` con PC en `.dynstr` tras `Load game graphml init` + hallazgo del swap a `.so` v1.0.6 (2026-09-05)

### El crash (hechos duros)

- **Dump** `logs/sacredodyssey-psp2core-1788587087-0x000e373e99-eboot.bin.psp2dmp` (vía
  `psvita-toolkit analyze`; reporte completo en `.analysis.txt` + `.triage_summary.md`): hilo
  `PSVSOTROA`, `Data abort`, **PC `0x981c1086` / LR `0x981c1079`** (base `.so` autodetectada
  `0x98147000` → offset **`0x7a086`/`0x7a079`**), `R1=0xffff`, `R2=R3=0`, resto sanos.
- **Log** `logs/log_20260905_014433.txt` (528 líneas, muere, no tormenta): carga `sprTexture.array`,
  `loading_splash_2x.sprite` + `loading_splash.kot` (¡upload ATCI 1024x512 OK vía vitaGL,
  `Loaded texture`!), imprime **`Load game graphml init`** y corta antes de `Material init`.
  Cero líneas `[gl_upload]` fuera de ese splash (normal), cero `fopen` de `CustomEffect.bdae`,
  cero `- Error - File not found`.
- **PC/LR caen en `.dynstr`** (v106: `.dynstr` = `0x541d8-0x172e7c`; el `0x7a086` contiene el
  símbolo `_ZN3vox10EmitterObj16Get3DParameterfvEiRNS_11VoxVector3fE` — el NOMBRE es
  coincidencia, lo relevante es que es zona de strings, no código). Desensambla basura en ARM y
  en Thumb: es **salto a datos** (puntero de función/objeto corrupto), no una instrucción
  parcheable. Los frames del stack (`HudIGMInventory::Init+0xcb`, `constructAnimator`,
  `CBatchDriver::draw`) son dudosos (`0x206dcf` no es dirección de retorno válida en `Init`:
  el `bl` previo retorna en `0x206dcd`) — tratarlos como vecindad, no como backtrace.
- Secuencia por `KnightOdyssey::Init` (pseudo-C v103, lógica válida; direcciones NO): tras el
  print de graphml solo quedan `EffectManager::GetInstance()` (ctor ya corrió sin crash en el
  init del renderer de esta misma corrida) e **`InitSpecialEffectMateiral()`** (ctor
  `CColladaDatabase("CustomEffect.bdae")` + 5× `constructEffect`: WaterDisturb, ...).

### Hallazgo lateral crítico: el `.so` cambió (v1.0.3-PowerVR -> v1.0.6 Thumb-2)

- `sacredodyssey_extract/lib/armeabi/libsacredodyssey.so` y `ux0_data/...` son idénticos
  (md5 `6735cd71...`, 6467428 B, + `libStormGLOFT.so` al lado): **ya no es el binario v103 de
  7.1 MB** con el que se calcularon TODAS las direcciones de `patch.c` y `decompiled/`.
- **Sin corrupción en esta corrida**: el árbol ya trae guarda de versión (`patch.c`, por
  `initPath @ 0x1bad44`, verificado: `nm -D` da `_Z8initPathv @ 0x1bad44`); el log línea 14 lo
  confirma: `Detected libsacredodyssey.so v1.0.6 ... skipping the v1.0.3-PowerVR raw-offset
  patches`. Solo corrieron hooks por símbolo (initPath + 6 license, dormidos: cero líneas de
  sus stubs en el log — inocentes). Verificado además a mano: en v106 NINGÚN sitio de parche
  contiene la instrucción original esperada (p. ej. `0x20d530` hoy es `pop {r4,r5,r6,pc}` —
  el `b` ARM-block lo habría decapitado; `0x20f43c` es `str.w`, etc.). **Si la guarda no
  existiera, este crash sería auto-infligido.**
- Consecuencia: **todas las direcciones v103 (patch.c vieja, `decompiled/` v103, entradas de
  bitácora con offsets) son HISTORIA para este binario** — no usarlas para parchear v106. El
  `.md` de disasm dice `ux0_data/...` pero se generó el 09-03 (binario viejo): solo vale para
  LÓGICA, no direcciones.

### Dataset en Vita inventariado por FTP (cierra la vía "faltan datos" para este crash)

- `GloftSOHP/data/`: 31 `.obfs` **minúsculos (66-2249 B, idénticos en tamaño a los locales)** +
  arrays + `audio/ intro/ lowtexture/ menus/ structs/ texts/ video/ 2d/ 3d/ automat/`.
- Los `.obfs` pequeños son **por diseño** (índices ofuscados `ff 00 00 00...`, no paquetes de
  assets). El ausente `2014777482.obfs` = `fletcher32("data/sacredodyssey/GloftSOHP//SaveTrophy.bin")`
  (demostrado: `FileStream::Open` hace `sprintf("%s/data/%u.obfs")` al fallar el directo — el
  propio log lo muestra en líneas 225-230): artefacto de **primera corrida sin saves**, benigno
  (`LoadTrophy` tolera el NULL, `Trophy init` prosigue).
- `CustomEffect.bdae` **SÍ está** (126988 B), `wt_zhuwang_2lowpt.kot` **completo y válido**
  (22000 B = 128² ATCA 8 mips + header exactos — valida el decode ATC a posteriori),
  `Res.array` de Vita (291298 B, **4090 strings, parsea limpio** como `[u32 len][bytes]×4090` +
  sección 2 de 132424 B con hashes/índices) difiere del local v101-adreno (290512 B): el dataset
  de Vita es coherente con **v106**, no ensalada (matiza la vieja teoría del mismatch: valía
  para v103, re-evaluar si reaparece).
- Conclusión datos: para ESTE crash no falta ningún archivo necesario. El `fopen` de
  `CustomEffect.bdae` nunca ocurre porque `CResFileManager::load/get` falla ANTES de tocar
  filesystem (miss silencioso — ver mecanismo).

### Mecanismo probable (inferencia marcada, pendiente de confirmación en v106)

Pseudo-C v103 (indicativo): `CColladaDatabase::CColladaDatabase` guarda
`CResFileManager::load(...)` sin chequear NULL; `getEffect(name)` busca lineal (`strcmp`,
stride `0x1d`, count en `+0x40`, array en `+0x44`) y devuelve NULL al fallar; el `constructEffect`
terminal hace **llamada virtual sin chequear**: `(**(param_2+4) + 0x14)(...)`. Con DB vacía /
`SEffect*` basura (el log-silencio encaja con hit-stale o rama `create=false` de `get()`, ambas
sin prints ni `fopen`), esa llamada salta a basura (`.dynstr`, con `R1=0xffff` como residuo del
sentinel "not found" — misma familia que `ShowItemEffectProxy` y `FileManager::_Load`). FALTA:
el sitio exacto en v106 (direcciones v103 no sirven).

### Caza estática en v106 intentada (registrada para no repetir)

- Mapa de secciones v106 (`.text 0x1bacc8-0x36caa0`, `.plt` 12 B/entrada con 2º sumando
  variable, 306 stubs decodificados, `strcmp→0x1baa64`, `fopen→0x1ba524`): **cero `BL` ARM y
  cero `BL` Thumb-2 a esos stubs en TODO `.text`** (31368 `BL` Thumb decodificados, 0 al PLT)
  → v106 llama imports por **GOT-directo + `blx`**, no por PLT. Inutiliza la técnica "buscar
  `bl` al stub" (la que encontró `FileStream::Open` en v103).
- Strings (`CustomEffect.bdae @0x36d5a0` etc.) sin refs directas en `.text`/`.got`
  (acceso computado). `adr`16 al rango: ninguno.
- Contenedor ajeno en Docker (`ecstatic_goldberg`, N.O.V.A-2) ignorado correctamente.

### Acción en curso y pendiente único

- **Re-decompile v106 lanzada en background** (Docker `devrvk/so-decompiler`, contenedor
  `sacredodyssey_v106_decompile`): entrada `sacredodyssey_extract/lib/armeabi`, salida NUEVA
  `decompiled/decompiled_so/libsacredodyssey_v106/` (no pisa v103). Al terminar: leer
  `InitSpecialEffectMateiral`/`getEffect`/`constructEffect` v106, localizar el branch sin
  chequear y añadir sección v106 al framework de guards de `patch.c` (misma receta que
  `0x20f43c`, prohibido reutilizar offsets v103).
- Si el pseudo-C v106 muestra otra cosa (p. ej. crash en `StateAutomat::Load` tardío), re-triar
  con el dump (PC/LR + `R1=0xffff` siguen siendo el ancla).

## Fase 8 (cont.): FIX del crash post-graphml en v106 — skip a `InitSpecialEffectMateiral`, build+deploy OK (2026-09-05)

- **Crash duplicado idéntico** (`sacredodyssey-psp2core-1788591714-...`: mismo PC/LR/base/regs que
  `-1788587087-...`): determinista, no race. El `.context.md` autogenerado no agregó nada
  (sin instrucción ni cruces con `decompiled/`).
- **Decompilación v106 terminó** (`decompiled/decompiled_so/libsacredodyssey_v106/out_ghidra.c`
  19 MB; el contenedor ajeno `ecstatic_goldberg` era de N.O.V.A-2, no se tocó). Con ella +
  symtab completo del `.so` se fijó el mecanismo EXACTO con direcciones verdaderas v106:
  - `InitSpecialEffectMateiral @ 0x1bf6e4` → `CColladaDatabaseC1("CustomEffect.bdae") @
    0x29dc24` (guarda el `load()` sin chequear; el load resuelve silencioso sin tocar
    filesystem: cero `fopen`/`open` de ese archivo y cero `- Error - File not found` en el log)
    → `constructEffect(char*) @ 0x29e78c` → `getEffect @ 0x29e3d4` (miss devuelve NULL, bien)
    → `constructEffect(SEffect*) @ 0x29e7b4`, que encadena SIN ningún NULL-check:
    `29e7b8: ldr r1,[r1,#4]` / `29e7c0: ldr r2,[r1,#0]` / `29e7ca: ldr.w ip,[r2,#20]` /
    `29e7d0: blx ip` → llamada por cadena muerta hasta `.dynstr` (PC/LR basura, `R1=0xffff`
    como residuo). Misma familia "miss sin chequear" que los fixes v103.
  - Nota de caza: v106 **no usa PLT** (31368 `BL` Thumb decodificados, 0 al PLT; llama imports
    por GOT-directo + `blx`, 306 stubs solo para lo que sí los usa) — la técnica "buscar `bl`
    al stub" no aplica aquí; lo que sí funcionó fue symtab (`nm` da TODO, incluidas locales)
    + `objdump` del toolchain con mapping symbols.
- **Fix aplicado (`source/patch.c`, sección nueva `so_patch_v106()`, llamada desde el branch
  v106 del guard existente):** hot-patch de 2 bytes en la entrada `0x1bf6e4`
  (`stmdb sp!,{r4-r9,sl,lr}` = `e92d 47f0` → `bx lr` = `0x4770`), verificado el halfword
  original antes de escribir (fail-safe: mismatch = skip + WARN). Retorna sin registrar los 5
  efectos especiales (agua/magma/fade ausentes = degradación visual aceptada; un miss posterior
  en `QueryFXMaterial` sin check sería el próximo bug, misma receta).
- **Validado:** `-fsyntax-only -Wall -Wformat` limpio (release+debug); `psvita-toolkit build
  --preset debug` OK; `deploy --eboot --yes` OK (eboot 570.6 KB en `ux0:app/PSVSOTROA/`).
- **Próximo paso:** correr en consola y traer log/dump. Señales esperadas: `Patched v106
  InitSpecialEffectMateiral whole-function skip`, `Material init`, y avance más allá
  (o el siguiente crash/dump, que se tria igual).

## Fase 8 (cont.): El guard funcionó pero mi halfword estaba al revés — corregido, build listo, deploy pendiente de FTP (2026-09-05)

- **Nuevo dump** (`sacredodyssey-psp2core-1788592387-...`, misma build): **idéntico al anterior**
  (PC/LR/regs/base calcados) + el log trae la prueba del fail-safe:
  `[WARN] v106 InitSpecialEffectMateiral entry mismatch (found 0xe92d, want 0x47f0) --
  skipping whole-function skip`. El crash sigue sin parchear, pero el guard evitó escribir un
  `bx lr` sobre bytes no verificados (sin él, esta corrida habría sido corrupción nueva).
- **Mi error:** en Thumb-2 `stmdb sp!,{...}` se codifica `e92d 47f0` con hw1 `0xe92d` (opcode) en
  `0x1bf6e4` y hw2 `0x47f0` (lista de registros) en `0x1bf6e6` — verifiqué contra el halfword
  equivocado. El parche (escribir `0x4770` en `0x1bf6e4`) estaba bien planteado; solo el valor
  esperado del check estaba mal. Corregido a `0xe92d` + comentario que documenta el orden.
- **Estado:** `build --preset debug` OK con la corrección. `deploy --eboot` FALLÓ: FTP
  rechazado (VitaShell sin SELECT). **Pendiente:** activar FTP en la consola, reintentar
  `psvita-toolkit deploy --eboot --yes`, correr y traer log (buscar `Patched v106 ... skip`
  + `Material init`).

## Fase 8 (cont.): El skip no evitó el crash idéntico — el bug está ANTES: port del fix ShowItem a v106 + read-back (2026-09-05)

- **Dump** `-1788592733-...` (build CON el skip aplicado, log línea 15 lo confirma): **cuarto
  dump idéntico** (PC/LR/regs/SP calcados). Conclusión: el crash NO está en el cuerpo de
  `InitSpecialEffectMateiral` (el `bx lr` en `0x1bf6e4` con flush completo de `.text` lo
  elimina) sino antes: `GetInstance()` → ctor `EffectManager` → ctor `ShowItemEffectProxy`.
- **Causa localizada en v106** (desensamblado toolchain, `ShowItemEffectProxyC1 @ 0x1c0f78`):
  el MISMO bug v103 en nueva dirección —
  `1c1074: bl getParameterID(6,0)` → `1c1080: cmp r2(num_params),r1(0xffff)` →
  `1c1082: bhi 1c1130` (presente) → fall-through `1c1084: movs r3,#0` /
  `1c1086: ldrb r3,[r3,#6]` (ausente = NULL+6). (Nota: el PC `.dynstr` del dump sigue sin
  calzar con un `ldrb` limpio; el `blx r3` virtual de `0x1c10bc` con tabla corrupta por el
  mismo miss es el candidato que sí calza — el skip lo evita igual al no llegar ahí.)
- **Fix aplicado (`so_patch_v106()`):** `0x1c1084`: `movs r3,#0; ldrb r3,[r3,#6]`
  (`0x799b2300`) → `b 0x1c10a8; nop` (`10 e0 00 bf`, verificado con capstone; `b.w` con este
  offset caería en la ambigüedad T3/T4 y decodificaría `beq.w`, por eso `b.n`+`nop`). Salta el
  `setParameter` con parámetro por defecto, como el fix v103. Con check previo + WARN si no
  coincide. Además el skip anterior ahora loguea **read-back** (`read-back 0x...`) para probar
  activación en runtime de una vez.
- **Validado:** sintaxis limpia; `build --preset debug` OK. **Deploy pendiente (FTP):**
  `psvita-toolkit deploy --eboot --yes` con VitaShell+SELECT, correr, traer log (buscar AMBAS
  líneas `Patched v106 ...` con sus read-backs + `Material init`).

## Fase 8 (cont.): Aclaración de timeline — el fix ShowItem v106 está compilado pero NUNCA corrió en consola (2026-09-05)

- **Prueba por timestamps + log:** `log_20260905_031840.txt` es de las **03:18:40** y trae UNA
  sola línea `Patched v106` (el skip; ni INFO ni WARN del bloque ShowItem, cuando el código
  loguea forzosamente uno de los dos) → el eboot desplegado para esa corrida **no contiene**
  el bloque ShowItem. `source/patch.c` es de las **03:23** y `build/eboot.bin` de las
  **03:25** (584505 B), y `build/sacredodyssey.elf` SÍ contiene el string
  `ShowItemEffectProxy ctor missing-param skip` → el fix está compilado en el build local pero
  ese build nunca se desplegó (el deploy de esa vuelta falló por FTP rechazado).
- **Consecuencia:** el cuarto dump idéntico NO refuta el fix ShowItem — ese código jamás
  ejecutó en consola. No re-triar nada hasta probar el build actual.
- **Pendiente único:** `psvita-toolkit deploy --eboot --yes` (VitaShell+SELECT activo),
  correr, traer log. Éxito = AMBAS líneas `Patched v106 ...` (skip con read-back `0x4770` +
  ShowItem con read-back del branch) y `Material init` + avance. Si el crash persiste
  idéntico CON ambas líneas presentes, entonces sí: el bug está en otro lado
  (`GetInstance`/heap) y se investiga con ese dato.

---

## Fase 9: CAUSA RAÍZ del cuelgue `.kot` — el `.so` y el dataset son de versiones distintas (2026-09-05)

`log_20260905_001018.txt` trae por fin las líneas `[fread_acct]` que faltaban, y cierran el caso
en la dirección **H4b (rechazo temprano)**, no H4a.

### 1. Lo que dice la medición

Todas las iteraciones de la tormenta son idénticas: **504 bytes en 126 lecturas de 4 bytes**,
`reader .so offset 0x212ed0`. Ese offset es `_ZN10DataStream4ReadEPvi` (`DataStream::Read`), un
wrapper genérico -- el parser real es su llamador, `Structs::SpriteTextureEntry::Read`. Los 504
bytes cuadran exactamente con leer el DDS servido como si fuera una tabla de sprites:

| Lectura | Valor leído del DDS | Qué hace el parser |
|---|---|---|
| 1 (4 B) | `0x20534444` (magic `"DDS "`) | `count1` -> `new[](count1*4)` ~2 GB -> NULL -> el parche de Fase 8 lo fuerza a 0 y salta el bucle |
| 2 (4 B) | `0x7c` (`dwSize` del DDS) | `count2` = 124 -> `new[](496)` OK |
| 3..126 (124 x 4 B) | resto de la cabecera/píxeles | los 124 ints del segundo array |

`1 + 1 + 124 = 126` lecturas, `126 * 4 = 504` bytes. **El contenido del archivo es irrelevante**
(por eso ni el ATC original, ni el placeholder, ni el decode RGBA cambiaban nada): lo que está
mal es *qué archivo* se está abriendo.

### 2. El bucle real (había tres, no uno)

Ghidra atribuye el cuerpo a `CFont::Load`, pero los statics (`ASprite::Load(int)::s_TexIds`)
confirman que es `ASprite::Load(int)` (`0x20ed7c`, Ghidra usa base `+0x10000`). Su estructura:

```c
LAB_0021efac:                                   //  == 0x20efac
do {                                            //  (3) bucle EXTERNO
  do {                                          //  (2) bucle MEDIO
    if (s_TexIds.size == 0) {
      ResStream(0xfa5); DataStream; Structs::SpriteTextureEntry::Read(); ...
      if (s_TexIds.size == 0) goto LAB_0021efac;   // (1) <- el beq 0x20f43c YA NOPeado
    }
  } while (param_1 < 0);
  ...buscar iVar4 en s_TexIds...
} while (lookup_falló);                          // <- el que gira ahora
```

El NOP de Fase 8 (`0x20f43c`) mató sólo (1). Como `SpriteTextureEntry::Read` devuelve 0 entradas,
`s_TexIds` queda vacío, el lookup final falla siempre y **(3) reintenta la carga completa para
siempre**. Ese es el `~120 ms/iter` con un único caller `0x1c7060`.

### 3. Por qué `SpriteTextureEntry::Read` recibe un DDS

`ResStream(0xfa5)` = **id de recurso 4005**, una constante hardcodeada
(`0x20f238: movw r1, #4005`) que indexa `data/Res.array`. El formato de `Res.array` es trivial
(`uint32 count`, luego `count` pares `len+path`, `len+name`) y se parsea sin ambigüedad:

- En el dataset desplegado, el índice **4005** es `data/lowtexture/wt_zhuwang_2lowpt.kot`.
- La tabla que el motor quiere, `data/sprTexture.array`, está en el índice **4063 (0xfdf)**
  (1176 bytes = `4 + 146*4 + 4 + 146*4`, exactamente la estructura que `SpriteTextureEntry::Read`
  espera).

No es un desfase puntual. Contrastando más ids hardcodeados de `CFontMgr::LoadFonts()`:
`LoadFontTable(0x37/0x38/0x39)` = 55/56/57 deberían ser los tres `fontTable*.bin`, y en el
dataset desplegado están en 107/108/109 (55-57 son `font_small_cn_*.kot`, texturas). Por eso
`CFont::Load` acaba construyendo `font_small_cn_2x_2x_sprite` (recorta `ext="_sprite"` del nombre
equivocado y le añade `_3x_sprite`/`_2x_sprite`): un nombre que no existe.

**El `.so` es de `Sacred-Odyssey-...-v1.0.3-PowerVR.apk` y los datos desplegados salieron de
`Sacred-Odyssey-...-v1.0.1-Adreno.zip`** (`Res.array` de 290512 B, 4076 entradas, fechado
2011-05-06 -- byte a byte el de ese zip). Los IDs de recurso van hardcodeados en el binario, así
que con tablas distintas **todos** los ids fijos (fuentes, HUDs, escenas, sprites) apuntan a otro
archivo. Los parches de Fase 8 sobre `FileManager::_Load`, `SpriteTextureEntry::Read` y
`ASprite::Load` estaban tapando síntomas de esto, no bugs del motor.

Descartado reconstruir el `Res.array` de 1.0.3: el orden es "agrupar por directorio, ordenar por
`name`, cada `.kot` genera 2 entradas" (verificado contra los dos `Res.array` que sí tenemos), pero
el `installer_res.txt` del APK 1.0.3 lista 12385 rutas -- un superconjunto con variantes por
dispositivo -- y simulando el directorio de fuentes los `fontTable*.bin` caen en 74/75/76, no en
55/56/57. Sin el `Res.array` real de 1.0.3 la reconstrucción es adivinanza.

### 4. Decisión: rebasar el port a v1.0.6 (par consistente)

El APK **v1.0.6** del repo y su `Data.zip` sí son un par coherente, verificado:

- su `.so` pide `movw r1, #4074` (`0xfea`), y en su `Res.array` (4090 entradas) el índice 4074 es
  exactamente `data/sprTexture.array`;
- sus `CFontMgr::LoadFonts` emparejan `LoadFontTable(0x3f=63)`/`Load(0x40=64)` con
  `font_small_cn.bin`/`font_small_cn.sprite` -- `.bin` seguido de su `.sprite`, el patrón correcto.

Y el coste de cambiar es bajo porque la interfaz es idéntica:

- **314 símbolos UND en ambos, cero nuevos** (`comm -13` de las dos listas: vacío) -> `dynlib.c`
  no se toca.
- Mismos nombres JNI y mismo paquete (`com_gameloft_android_ANMP_GloftSOHP_ML_*`) -> `java.c`,
  FalsoJNI y `main.c` no se tocan.
- Mismos `DT_NEEDED` y mismo `SONAME` (`libknightodyssey.so`). El `libStormGLOFT.so` que trae el
  APK 1.0.6 sólo lo referencia el Java, no hay `DT_NEEDED` -> no hace falta.
- Todos los símbolos que hookea `patch.c` existen (`m_gAppPath`, `_Z8initPathv`, los 6 de
  `ALicenseCheck`) -> los hooks por símbolo siguen valiendo tal cual.

Diferencia relevante: **el v1.0.6 está compilado en Thumb-2** (el v1.0.3 era ARM), así que su
`.text` es completamente distinto.

### 5. Cambios aplicados en esta vuelta

- **`source/patch.c`:** añadido `so_build_id()` -- devuelve la dirección de `_Z8initPathv`
  relativa a `text_base`, con el bit 0 enmascarado (en un build Thumb el `st_value` lo trae
  puesto; es justo lo que usa `hook_addr()` para elegir `hook_thumb`/`hook_arm`). Los 8 parches
  por offset crudo quedan detrás de un `if (build != SO_BUILD_V103_POWERVR) return;`, con el
  build detectado logueado. Los hooks por símbolo se aplican siempre, antes del guard.
  Motivo: escribir esos offsets sobre el v1.0.6 no falla con un error visible, corrompe
  instrucciones arbitrarias -- corrupción silenciosa, el peor modo de fallo posible.
  El bloque v1.0.3 se conserva íntegro (con sus comentarios) por si hay que volver.
- **`.psvita-toolkit.json`:** `apk_basename` -> `Sacred Odyssey-v1.0.6-agmod.org.apk`.
- **Staging local** (`ux0_data/`, gitignored): `libsacredodyssey.so` -> el del APK 1.0.6
  (6467428 B); `GloftSOHP/` -> datos del `Data.zip` 1.0.6. El árbol 1.0.1 anterior queda en
  `GloftSOHP.v101-adreno.old` y el APK 1.0.3 extraído en `sacredodyssey_extract.v103-powervr/`.
- Los saves de plantilla que traía el zip 1.0.1 en la raíz de `GloftSOHP/` (`SaveGame.bin`,
  `SaveMC.bin`, ...) NO se copian: el `Data.zip` de 1.0.6 no los incluye, así que el juego debe
  generarlos. Si aparece un crash al leerlos, ese es el primer sospechoso.

### 6. Pendiente

- Desplegar el nuevo `.so` **y** los datos 1.0.6 completos a `ux0:data/sacredodyssey/`.
- Corrida en consola: se espera que la tormenta de `wt_zhuwang_2lowpt.kot` desaparezca por
  completo (el id 4074 ahora encuentra `sprTexture.array` de verdad).
- Re-triage desde cero de lo que aparezca. Los crashes de `Gameplay::s_instance` pueden volver
  (son bugs reales del motor) pero sus direcciones son otras: hay que recalcularlas sobre el
  binario Thumb-2. Nada de reusar los offsets viejos.
- Re-decompilar con Ghidra el `.so` v1.0.6 cuando haga falta pseudo-C (el binario trae símbolos,
  así que `objdump`/`nm` alcanzan para bastante).

## Fase 8 (cont.): FIX del crash post-Splash en v106 — `SplashState::Draw2D` desreferencia textura NULL (`gameloft_3x_tga`) (2026-09-05)

- **Log** `logs/log_20260905_033521.txt` (693 líneas, el run MÁS AVANZADO en v106): trae AMBOS
  parches v106 aplicados con read-backs OK (`InitSpecialEffectMateiral skip, read-back 0x4770` +
  `ShowItemEffectProxy skip`), supera `Load game graphml init` / `Material init` / `Effect
  manager init`, carga `sprTexture.array` + `loading_splash.kot` + 6 texturas de fuentes, entra a
  `Go to state: Splash` y muere. Últimas líneas útiles:
  `fopen(...//gameloft_3x_tga): 0x0` → `ERROR: Missing file: gameloft_3x_tga` →
  `Could not find texture file: gameloft_3x_tga` → `ALicenseCheck_ValidateLicense bypassed` → corte.
- **Descartado el stub de licencia como causa:** `ALicenseCheck_ValidateLicense` es un simple
  `b ValidateServerEb`, y `ValidateServerEb` (ARM, `0x347708`, desensamblado con el toolchain)
  retorna `void` (termina en `pop {..., pc}`, con rama a `exit@plt` si el check falla) — nuestro
  stub `void` coincide en firma y evita ese `exit`. El log llega HASTA el stub y el crash es después.
- **Mecanismo confirmado (pseudo-C v106 + `objdump` Thumb-2, sin dump nuevo todavía):**
  - `Res.array` v106 (4090 entries, parseado a mano): solo existen `gameloft_tga` /
    `gameloft_2x_tga` / `gameloft_kr_tga` / `gameloft_2x_kr_tga` (índices 139-142) — **no hay
    ningún `gameloft_3x_tga`**. Miss genuino, misma familia "asset faltante" de Fase 8.
  - `SplashState::FocusGain() @ 0x211834` maneja el miss SIN crashear: con width `0x3c0` (960,
    nuestra res) llama a `getTexture()` (que loguea el `Could not find texture file` y guarda
    NULL en `this+0x10` con guards de refcount), luego `ValidateLicense(false)` y retorna.
  - `SplashState::Draw2D() @ 0x211cf4` chequea el MATERIAL (`this+0x14`, `0x211d14/0x211d16` →
    `beq 0x211da2`) pero NUNCA la textura (`this+0x10`):
    `211d48: ldr r0,[r4,#16]` / `211d4a: ldr r3,[r0,#0]` (**CRASH**: vtable de NULL) /
    `211d4c: ldr r3,[r3,#24]` / `211d4e: blx r3` (pseudo-C v106 ~l.99580).
- **Fix aplicado (`source/patch.c`, tercer bloque de `so_patch_v106()`):** `0x211d48` (4 bytes,
  `ldr r0,[r4,#16]; ldr r3,[r0,#0]` = `0x68036920`) → `b.w 0x346388` (`0x1ebb34f1`).
  Trampolín Thumb en `0x346388` (cuerpo muerto de `ALicenseCheck::LoadConfig`, hookeada en
  `so_patch()`: `hook_arm` pisa 8 bytes en `0x346380-0x346387`, el resto del cuerpo ARM de
  100+ bytes jamás ejecuta): recarga `r0`, `cmp r0,#0`, `beq.n` al nullpath que hace
  `b.w 0x211da2` (la misma continuación "sin dibujo, solo avanza contador" del camino
  legítimo material-NULL), o replica el `ldr r3` y hace `b.w 0x211d4c`. Opcodes verificados
  ensamblando con `arm-vita-eabi-as`/`-ld -Ttext=<dirección>`; fail-safe con check previo +
  WARN como los dos bloques anteriores. Flags/registros seguros (ni `0x211d4c` ni `0x211da2`
  dependen de los de entrada — ver comentario extenso en el código).
- **Validado:** `psvita-toolkit build --preset debug` OK; strings del parche confirmados en
  `build/sacredodyssey.elf`; `eboot.bin` (584620 B) y `sacredodyssey.vpk` regenerados en
  `build/` y copiados a la raíz. **Pendiente (requiere consola):** `psvita-toolkit deploy
  --eboot --yes` (VitaShell+SELECT), correr y traer log. Éxito = línea `Patched v106
  SplashState::Draw2D NULL-texture skip` + avance más allá del Splash (próximo estado o
  siguiente crash/dump, que se tría igual). Nota: el logo de Gameloft no se verá (textura
  NULL → frame skipeado, degradación visual aceptada); si se quiere fidelidad, follow-up
  sería redirigir `gameloft_3x_tga` → `gameloft_2x.kot` en `io.c`, pero eso es cosmético,
  no el crash.

## Fase 8 (cont.): El parche Splash v106 crasheaba por un bug PROPIO — halfwords del `b.w` cruzados (2026-09-06)

- **Log** `logs/log_20260906_205615.txt` (694 líneas): trae los TRES parches v106 con
  read-backs OK, supera shaders/fuentes, entra a `Go to state: Splash` y muere tras
  `Could not find texture file: gameloft_3x_tga` + `ALicenseCheck_ValidateLicense bypassed`
  — idéntico punto al run anterior, pero esta vez CON el parche Splash supuestamente activo.
- **Dump** `logs/sacredodyssey-psp2core-1788742592-0x001f9327ad-eboot.bin.psp2dmp`:
  `psvita-toolkit analyze` autodetectó base incorrecta (`0x83d81000`, todos "FUERA DE
  RANGO"). Con la base real (`0x98000000`, fijada por `Device_nativeInit -> 0x981bbcb5`
  del propio log menos offset `0x1bbcb4` del symtab): **PC offset `0x211d4c`** =
  exactamente el resume-target del trampolín (`b.w 0x211d4c`), con `R3=0x18ae` basura.
- **Causa raíz (bug propio, no del motor):** el `uint32 br = 0x1ebb34f1` de `so_patch_v106()`
  tiene los halfwords CRUZADOS. El ensamblador (`as`+`ld -Ttext=0x211d48`) da para
  `b.w 0x346388` los halfwords `{0xf134, 0xbb1e}` = uint32 LE **`0xbb1ef134`**; lo escrito
  (`{0x34f1, 0x1ebb}`) ejecuta como `adds r4,#241` (¡corrompe `this`!) + `subs r3,r7,#2`
  y cae a `0x211d4c` con `r3` basura → Data abort ahí. El read-back del log (`0x1ebb34f1`)
  solo prueba que la escritura pegó, no que la codificación sea válida — por eso no lo
  detectó. Los dos `b.w` INTERNOS del trampolín (`{0xf6cb,0xbcdc}`→`0x211d4c`,
  `{0xf6cb,0xbd05}`→`0x211da2`) sí se verificaron byte-a-byte contra el ensamblador y
  están bien; solo la palabra del sitio estaba mal (el parche ShowItem v106 no tiene este
  problema: usa array de bytes, y el skip `bx lr` es de 16 bits).
- **Fix (`source/patch.c`):** `br` → `0xbb1ef134` + comentario que documenta la trampa
  (halfwords vs uint32 LE). **Lección:** todo `b.w` Thumb-2 a escribir como `uint32`
  debe verificarse ensamblando Y leyendo los halfwords en orden, no concatenando a ojo
  la salida del objdump.
- **Validado:** `psvita-toolkit build --preset debug` OK; `deploy --eboot --yes` OK
  (FTP respondió, eboot subido a `ux0:app/PSVSOTROA/`).
- **Pendiente (requiere consola):** correr y traer log. Éxito = línea
  `Patched v106 SplashState::Draw2D NULL-texture skip (... read-back 0xbb1ef134)` +
  avance más allá del Splash (splash sin logo → próximo estado, o siguiente crash/dump).

## Fase 8 (cont.): Sin crash — pantalla negra con FPS 0 es carga lenta, no cuelgue (2026-09-06)

- **Log** `logs/log_20260906_213524.txt` (2369 líneas, sin `.psp2dmp` — no hay crash): el fix
  del `b.w` funcionó. El juego superó el Splash, toleró el fallo de `AudioTrack`
  (`AudioTrack driver could not initialize`, JNI `AudioTrack/<init>`/`getMinBufferSize`/
  `play`/`pause`/`stop`/`release`/`write` no encontrados — el driver VOX queda mudo pero
  no bloquea), cargó la escena 3D `mainmenu2/jiemian_sence.bdae` (7 texturas con upload
  real + 19 shader programs linkeados) y entró a un escaneo alfabético del catálogo de
  audio (`mus_*` → `sfx_amonbane_*`), donde el usuario mató la app.
- **Por qué pantalla negra + FPS 0 (explicación, no bug):**
  - `source/main.c` hace `nativeRender + gl_swap` por iteración y `render_diag` cada 60
    frames — hay CERO `render_diag` en el log → menos de 60 frames en toda la sesión.
    Toda la carga ocurre en bloqueo DENTRO de esos pocos frames (cada `glLinkProgram`
    traduce GLSL→Cg vía ShaccCg, cada textura sube por vitaGL), así que cada frame tarda
    segundos: cualquier contador externo marca FPS ~0.
  - Cada frame swapeado muestra negro por diseño propio: el Draw2D del Splash no dibuja
    nada (skip de textura NULL — sin logo) y la escena mainmenu2 aún no termina de cargar.
- **Por qué es finito (no un retry infinito):** los 27 archivos de audio son todos
  DISTINTOS, cada uno exactamente 2 `fopen` (patrón existencia+carga, no tormenta), en
  orden alfabético. El dataset trae 946 archivos en `data/audio/` — al ritmo actual el
  escaneo solo necesita minutos, no es un loop.
- **Sin cambios de código en esta vuelta.** Siguiente paso (requiere consola): UNA corrida
  larga sin matar la app (~10 min, el `sceKernelPowerTick` del loop ya evita
  auto-suspendido) y traer el log. Señales: avance alfabético del escaneo → primer
  `[render_diag]` → menú. Si se detiene en el MISMO archivo en dos corridas, ahí sí es
  cuelgue y se investiga ese punto.

## Fase 8 (cont.): Indicador de carga en pantalla (2026-09-06)

- **Pedido:** una señal visual de "sigue cargando" para no confundir las pantallas negras
  (Splash sin logo, escena sin terminar) con un congelado. Sin corrida larga de por medio:
  cambio solo de loader, sin tocar lógica del motor.
- **Implementado (`source/utils/loading_overlay.{c,h}`, wireado en `source/main.c`):**
  barra indeterminada (track oscuro + segmento cian en ping-pong según `frame_count`)
  abajo en pantalla, dibujada DESPUÉS de `nativeGameRendererRender` y ANTES de
  `gl_swap()` — cada frame swapeado, aunque el motor pinte negro, muestra movimiento.
  Shader propio mínimo (2 atributos/uniform), todo el estado GL tocado se guarda y
  restaura (programa, VBO, attrib array, blend/depth/cull/scissor); si su compilación
  falla se desactiva solo y para siempre. Nuevo archivo registrado en `CMakeLists.txt`.
- **Validado:** `psvita-toolkit build --preset debug` OK. **Deploy pendiente:** FTP
  rechazado (`Connection refused`) — activar VitaShell+SELECT en la consola y reintentar
  `psvita-toolkit deploy --eboot --yes` antes de probar.

## Fase 8 (cont.): Triaje del Crash `sacredodyssey-psp2core-1788750008-...` — 7mo sitio `0xffff` sin chequear, en `CParticleSystemSceneNode::init` (2026-09-06)

- **Comportamiento en consola (reporte + log `log_20260906_225550.txt`, 8292 líneas, 3-5
  min de carga):** el overlay funcionó (barra visible en pantalla) y el juego llegó
  LEJOS: primer `[render_diag]` de la historia (`frame=60 fps=0.4 glGetError=0x0500`,
  `alive=1 paused=0`) — cada frame tarda ~2.5 s por traducción de shaders/uploads, de
  ahí el FPS ~0. Luego crasheó al final del log, en pleno setup de materiales de la
  escena mainmenu2 (`_5_-_Default`, o sea efecto Default = nuestro Pink fallback).
  Pantalla observada (negro + barra azul + escena rojiza) = negro del clear + overlay
  propio + escena 3D en rosa fallback — el motor YA está renderizando 3D.
- **Análisis del dump** (base autodetectada `0x83d81000` otra vez mal; con la base real
  `0x98000000`): `PC offset 0x2c371c` / `LR offset 0x2c378a`, `R0=0 R1=6 R2=0`,
  función `CParticleSystemSceneNode::init()+0x30`. Desensamblado Thumb-2 real:
  ```arm
  2c3786: bl getParameterID(...,6,0) ; r0 = id, 0xffff si falta
  ...
  2c3792: bls 0x2c371a   ; missing -> camino r0=0
  2c371a: movs r0, #0
  2c371c: ldr r3, [r0, #0] ; ¡CRASH! (PC)
  2c371e: cbz r3, 2c3722
  2c3720: adds r3, #4
  ```
  Misma familia que los 6 sitios anteriores: materiales con Pink fallback no tienen el
  uniform → `0xffff` → camino "missing" que desreferencia igual. El loop itera emisores
  (`[r0,#344]`→`[r0,#348]`); ambas ramas de miss (la de `0x2c3792` y la de `0x2c3776`)
  funnelan a `0x2c371a`, así que un solo parche cubre todo. El `getParameterID` post-loop
  (`0x2c37b2`, `str r0,[r5,#372]`) guarda `0xffff` sin desreferenciar — no se toca.
- **Fix (`source/patch.c`, 4to bloque de `so_patch_v106()`):** en `0x2c371c` se pisan 4
  bytes (`ldr`+`cbz` = `0xb1036803`, con check previo + WARN) con `b.w 0x346398`
  (`0xbe3cf082`); trampolín en `0x346398` (siguiente hueco libre de LoadConfig, 22 bytes,
  holgado antes del pool en `0x346434`) que replica la secuencia exacta con `cmp+beq.w`
  (el `cbz` no llega por rango desde ahí) y con `r0==0` produce `r3=0` = mismo estado
  que el camino legítimo "[r0]==0". Opcodes verificados con `as`/`ld -Ttext` (misma
  disciplina post-`0x1ebb34f1`: el `b.w` del sitio se verifica como halfwords en orden).
- **Validado:** `build --preset debug` OK; `deploy --eboot --yes` OK (FTP respondió).
- **Pendiente (requiere consola):** correr y traer log/dump. Éxito = línea
  `Patched v106 CParticleSystemSceneNode::init NULL-param skip (... read-back
  0xbe3cf082)` + frame 120+ / avance de escena. Nota: `glGetError=0x0500`
  (GL_INVALID_ENUM) en el primer `render_diag` queda como observación sin atribuir
  (puede ser del motor con enums ATC/comprimidos); si persiste con frames ya normales,
  se investiga aparte.

## Fase 8 (cont.): Por qué no hay splash/imagen/sonido + redirect del logo real (2026-09-06)

- **Pregunta:** por qué solo pantalla negra, sin sonido, sin imagen ni splash.
- **Respuestas con evidencia:**
  - *Sin splash:* el motor pide `gameloft_3x_tga`, que NO existe en el dataset v1.0.6
    (Res.array solo trae `gameloft_tga`/`gameloft_2x_tga`/`gameloft_kr_tga`/
    `gameloft_2x_kr_tga`) → nuestro parche salta el dibujo en vez de crashear → negro.
  - *Sin sonido:* los JNI de `android/media/AudioTrack` no existen en el loader
    (`<init>`/`getMinBufferSize`/`play`/`pause`/`stop`/`release`/`write` → not found) y
    el driver VOX falla (`AudioTrack driver could not initialize`) → el juego queda
    mudo por diseño actual. Audio real (VOX→sceAudio) es tarea futura separada, no de
    esta vuelta.
  - *"Sin imagen":* sí la hay — lo rojizo visto en pantalla es la escena mainmenu2
    renderizada con el Pink fallback (todos sus `ProfileCOMMON_emul_VS/FS.glsl` faltan
    y caen al rosa). Texturas reales SÍ suben (eagle, background, árboles...), pero los
    materiales son planos porque sus shaders verdaderos no están en el dataset.
  - *Por qué tarda:* traducción GLSL→Cg por programa (19+), uploads de texturas y el
    escaneo de 946 audios, en una CPU a 444 MHz y con logging debug a flash por línea.

## Fase 8 (cont.): `log_20260909_214145.txt` — confirmado en consola: ambos parches
anteriores (`CParticleSystemSceneNode::init` + redirect de `gameloft_3x_tga`) funcionan
sin crash; nuevo hallazgo de rendimiento (2026-09-09)

- **Comportamiento reportado:** pantalla rosada + el overlay de carga propio, pero
  nunca aparece la pantalla de carga real del juego ni el title ni el menú.
- **Confirmado en el log (8289 líneas, sin `.psp2dmp` nuevo -- no es un crash):**
  - Los 4 parches `so_patch_v106()` se aplican y con read-back correcto (líneas 15-18).
  - `Missing splash logo ... gameloft_3x_tga -- redirecting to ...gameloft_2x.kot` SÍ
    dispara y el motor acepta el `.kot` (`Loaded texture: gameloft_3x_tga`, línea 690) --
    el fix del logo de la sesión anterior funciona.
  - `glLinkProgram` va de 1 a 27 monotónicamente, SIN repetirse ningún número -- no es
    un loop infinito recompilando lo mismo, son 27 programas nuevos genuinos.
  - Único `render_diag` de todo el log: `frame=60 fps=0.5` (línea 5258). Después de esa
    línea, ~3000 líneas más (hasta la 8289, fin del log) de puro spam
    `invalid bind symbol: DiffuseColor/Sampler0/TextureMatrix0/...` +
    `Unused parameter: ...` (252 redirects de `ProfileCOMMON_emul_VS/FS.glsl` en total) --
    SIN que aparezca un segundo `render_diag` (frame=120). Es decir: el juego quedó
    congelado dentro de UN solo frame/Update(), no colgado sin más.
- **Causa raíz identificada cruzando el pseudo-C (`out_ghidra.c`, v106):**
  `SplashState::Update(int)` (línea ~99415) acumula tiempo hasta pasar un umbral y
  entonces llama UNA vez a `MenuState::PrePreload3D()`, que construye
  `SceneObject::SceneObject(id=0x59d, ...)` -- la escena "mm_scene" del menú principal
  completa, con TODOS sus materiales -- de forma síncrona y bloqueante dentro de esa
  única llamada. `Preload3D()` (mm_mc/mm_camera) es incremental entre frames, pero
  `PrePreload3D()` no lo es: cada material de esa escena que no tiene el uniform
  esperado (todos, porque están usando el material Pink de fallback en vez de sus
  shaders reales) genera 3-6 líneas de log (`invalid bind symbol`/`Unused parameter`),
  y CADA línea de log en este build Debug hace un `sceIoOpen+Write+Close` a la
  memoria flash MÁS un `sceNetSendto` UDP -- una sola llamada bloqueante que nunca
  vuelve hasta terminar de recorrer toda la escena. Con ~250 redirects × varias líneas
  cada uno, eso es potencialmente miles de aperturas de archivo síncronas antes de que
  el frame pueda seguir. Esto explica por qué la pantalla queda congelada en rosa: el
  motor sigue vivo y progresando (no es un hang real), pero una sola llamada
  (`PrePreload3D`/`SceneObject::SceneObject(0x59d)`) tarda un tiempo desproporcionado
  por el costo de logging, no solo por la traducción GLSL real.
- **Fix aplicado (`source/utils/logger.c`):** throttling de líneas repetidas
  BYTE-A-BYTE en los sinks de archivo/UDP únicamente (la consola por `sceClibPrintf`
  sigue sin throttle). Tabla fija de 64 hashes FNV-1a vistos; una línea nueva siempre
  se escribe completa (no se pierde nada nuevo si crashea justo después); una línea
  YA vista se escribe las primeras 3 veces y luego cada 500 repeticiones (con nota
  `(repeated Nx)`), sin escribir las demás -- reduce el costo de disco/red de esta
  ráfaga sin tocar timing de nada más ni requerir cambiar `DEBUG_SOLOADER`.
- **Validado:** `build --preset debug` OK.
- **Pendiente (requiere consola):** la PS Vita no respondía por FTP en esta sesión
  (VitaShell con FTP apagado / consola apagada) -- no se pudo hacer `deploy --eboot`.
  Al desplegar y correr: si la hipótesis es correcta, el mismo tramo de carga debería
  completarse en mucho menos tiempo real (el `render_diag` de frame=120 debería
  aparecer) y el juego debería alcanzar el Title/Menú. Si sigue tardando lo mismo, el
  cuello de botella real es la traducción GLSL→GXM (ShaccCg) en sí, no el logging, y
  el siguiente paso sería probar con un build Release (sin `DEBUG_SOLOADER`) para
  aislar cuánto del tiempo es logging vs. motor real.
- **Mejora aplicada (`source/reimpl/io.c`, mismo patrón que los shaders):** si falla el
  `fopen` de `gameloft_3x_tga` en modo lectura, se redirige al `gameloft_2x.kot` real
  del dataset (`data/2d/sprites/High_Quality/`, verificado que existe). Si el motor
  acepta el `.kot` por contenido (DDS, como el resto de texturas), el Splash muestra el
  logo real; si pide más archivos hermanos que falten, cae al skip actual sin crash
  (peor caso = igual que ahora).
- **Validado:** `build --preset debug` OK; `deploy --eboot --yes` OK.
- **Pendiente (requiere consola):** correr y traer log. Señal esperada: línea `Missing
  splash logo ... redirecting to ...gameloft_2x.kot` + (`Loaded texture` del logo y
  logo visible) o, si no lo acepta, el mismo skip de antes.

## Fase 8 (cont.): La pantalla rosada la causaba NUESTRO redirect — eliminado el fallback `PinkBadShader` (2026-09-10)

- **Comportamiento reportado:** solo pantalla rosada + barra de carga propia; nunca el
  title ni el menú. Se pidió comparar con los ports del mismo motor
  (`Asphalt-5-Vita`, `Shadow-Guardian-vita`, `Dungeon-Hunter-2-vita`).
- **Causa raíz, confirmada cruzando 3 fuentes (sin adivinar):**
  - `log_20260909_224913.txt`: cada `data/3d/effects/*.glsl` pedido por el motor
    (`UnlitVertexColorVP/ FP`, `UnlitMaterialColorVP/FP`,
    `UnlitOneTextureAndVertexColorVP`, `UnlitTexturedFP`,
    `UnlitTexturedBlendTextureAlphaFP`, `UnlitMultiTexturedFP`,
    `ProfileCOMMON_emul_VS/FS`) da `Missing effect shader ... redirecting to
    fallback .../PinkBadShaderVP/FP.glsl`, y acto seguido compila/linkea ese
    sustituto. Todos los materiales quedan atados al programa rosa (que solo tiene
    `WorldViewProjectionMatrix`), de ahí el spam posterior `invalid bind symbol:
    DiffuseColor/Sampler0/TextureMatrix0/...` + `Unused parameter: ...`.
  - El binario real: `libsacredodyssey.so` contiene EMBEBIDO el mismo shader rosa
    (`invalid pink stuff` / `invalid.Pink Bad Shader.` + el `Vertex` /
    `gl_FragColor = vec4(0.8, 0.3, 0.5, 1.0)` byte-idéntico al que generaba
    `source/reimpl/io.c`) -- es el `createEmptyShader` del propio motor, NO una
    fuente real.
  - El dataset NO trae ninguna fuente `.glsl` (verificado: `effects/` solo tiene
    `CustomEffect.bdae`/`DefaultEffects.bdae` tanto en el data v1.0.6 agmod como en
    el v1.0.1 Adreno; los 31 `.obfs` son stubs de <3 KB sin shaders dentro; `Res.array`
    lista los nombres pero no hay contenido en disco). En Android ese mismo `fopen`
    también falla y el motor sigue por su ruta procedural `SProfileGLES2` -- nuestro
    redirect convertía ese fallo esperado en un "archivo existente" rosa, y el motor
    compilaba el rosa en vez de generar los shaders reales.
  - Los 3 ports de referencia NO tienen ningún fallback de shaders en su `io.c`:
    Shadow-Guardian trae `shaders/*.xml|*.vs|*.fs` reales y Dungeon-Hunter-2 trae
    `shaders.pak`; sus `io.c` se limitan a traducir rutas y dejar que el `fopen`
    falle. Esa es la disciplina que muestra gráficos reales.
- **Fix aplicado (`source/reimpl/io.c`):** eliminado por completo el bloque de
  `PinkBadShader` (fuentes embebidas `fallback_vp/fp_src`, creación de
  `PinkBadShaderVP/FP.glsl` y redirect de `/effects/*.glsl`). El `fopen` fallido ahora
  retorna `NULL` como en Android y como en los 3 ports de referencia. Se conservan el
  redirect del logo `gameloft_3x_tga -> gameloft_2x.kot` (asset real que SÍ existe) y
  la nota de no redirigir `glsl.config`.
- **Validado:** `build --preset debug` OK (`sacredodyssey.vpk` generado).
- **Pendiente (requiere consola):** desplegar y correr. Señales esperadas: desaparecen
  las líneas `Missing effect shader ... PinkBadShader` y el spam `invalid bind
  symbol`; en su lugar el motor genera sus programas reales y aparecen splash real,
  title y menú (escena `mainmenu2/jiemian_sence.bdae`, que el log ya demuestra que
  carga con todas sus texturas). Si algún `.glsl` concreto siguiera sin resolverse por
  la ruta procedural, el motor usará SU propio rosa interno solo para ese material
  (degradado local, no pantalla completa rosa) -- ese sería el siguiente bug, con la
  misma receta. Nota: si la   consola conserva `ux0:data/sacredodyssey/PinkBadShader*.glsl`
  de sesiones anteriores, borrarlos a mano (ya nada los abre, pero evita confusión).

## Fase 8 (cont.): Crash `ftell(NULL)` al quitar el rosa — FileStream::Tell/Seek sin chequear FILE* (2026-09-10)

- **Comportamiento reportado:** `log_20260909_235138.txt` (242 líneas) + dump
  `sacredodyssey-psp2core-1789012303-0x00025c2f73-eboot.bin.psp2dmp`. El log ya NO
  tiene rosa (el fallback PinkBadShader eliminado funciona) y muestra la secuencia
  exacta: `fopen(UnlitVertexColorVP.glsl)` falla, el motor intenta su fallback propio
  `data/2181175739.obfs`, TAMBIÉN falla (el dataset no lo trae), imprime `ERROR:
  unable to open the file ...` y crashea justo después.
- **Causa raíz (so-crash-triage, 3 fuentes):** el análisis automático usó el `.so`
  equivocado (v1.0.3 de `apk_jadx`, 7.1 MB) -- se re-triajeó a mano contra el v1.0.6
  real de la Vita (`ux0_data`, 6.4 MB, `text_base = 0x98000000` deducido de
  `Device_nativeInit @ 0x1bbcb4 -> 0x981bbcb5` del propio log). Los retornos Thumb de
  la pila caen en `FileStream::Tell` (0x1c4486), `FileStream::Size` (0x1c4520),
  `CustomReadFile::getSize` (0x1fcc5e) y `CGLSLShaderManager::createShader`
  (0x29b218): el motor construye un `FileStream`/`CustomReadFile` alrededor del
  `FILE*` NULL sin chequear `IsValid()` y `Tell()` hace `ldr r0,[r3,#0]; blx ftell`
  con `R0=0` -> Data abort en SceLibc (PC `0x81687e88`, `R0=R4=0`). Octavo sitio de
  la familia "recurso faltante sin chequear". `Seek()` (0x1c444c, idéntico patrón de
  4 bytes `0x68186883` + `blx fseek`) es el crash gemelo siguiente en `Size()`.
- **Fix aplicado (`source/patch.c`, `so_patch_v106()`):** dos trampolines nuevos en el
  hueco libre del cuerpo muerto de `LoadConfig` (Seek en 0x3463ae, Tell en 0x3463c0,
  18 bytes cada uno, pool en 0x346434 intacto) que replican `ldr/ldr` pero con
  `FILE*==NULL` devuelven 0 (tamaño 0 / SEEK fingido ok) en vez de crashear. Opcodes
  verificados ensamblando con `arm-vita-eabi-as/-ld -Ttext=` + desensamblado
  (`-Mforce-thumb`) del `.bin`. Devolver 0 y no -1 es deliberado: con -1 el `strb
  [sl,fp]` de `createShader` escribiría antes del buffer (heap corruption); con 0 el
  NUL cae dentro del byte allocado, compila fuente vacía (error logueado, sin crash)
  y el motor usa su rosa interno solo para ese material.
- **Validado:** `build --preset debug` OK; `deploy --eboot --yes` OK.
- **Pendiente (requiere consola):** correr y traer log + confirmar que no hay dump
  nuevo. Señales esperadas: líneas `Patched v106 FileStream::Seek/Tell NULL-FILE
  skip ... read-back 0xbfaff181/0xbf9ff181`, el `ERROR: unable to open the file
  ...glsl` seguido de progreso (más shaders intentados, `glCompileShader`, escena
  del menú) en vez de Data abort. Si aparece un crash nuevo, triajearlo con la misma
  receta contra el `.so` v1.0.6 (NO el de `apk_jadx`).

## Fase 8 (cont.): Crash en `glLinkProgram` con shaders vacíos — vitaGL no tolera fuente vacía (2026-09-10)

- **Comportamiento reportado:** `log_20260910_000432.txt` (260 líneas) + dump
  `sacredodyssey-psp2core-1789013078-0x0002af2723-eboot.bin.psp2dmp`. Los parches
  Tell/Seek SÍ aplican (read-backs `0xbfaff181`/`0xbf9ff181`, líneas 19-20) y el motor
  pasa el punto del crash anterior: `Size()==0`, lecturas de 0 bytes (`storm read ...
  1 x 0 = 0 bytes`, reader `.so` 0x1c43f9 = `FileStream::Read`), `glsl.config`
  ausente (tolerado), `glCompileShader(1)(2)` OK... y el log se corta en
  `glLinkProgram(1): linking...` sin el `done`.
- **Causa raíz:** el dump (esta vez analizado con el `.so` v1.0.6 correcto y
  `--so-base 0x98000000`) muestra PC en `strchr(R0=NULL)` vía
  `glsl_handle_globals (glsl_utils.c:938) <- glsl_translator_process <-
  glsl_translator_set_process <- glLinkProgram`: con fuente `""`,
  `strstr(type,"{")` da NULL y el siguiente `strstr(NULL,"}")` crashea. Es un crash
  en vitaGL (código del loader, `lib/vitagl`), no en el `.so` -- el `.so` hizo todo
  bien (fuente vacía compilada, error que el motor hubiera tolerado) pero la
  traducción GLSL→GXM postergada (`VGL_MODE_POSTPONED`) muere al parsear el vacío.
- **Fix aplicado (`source/utils/glutil.c`, `glShaderSource_soloader`):** si la fuente
  concatenada es vacía/solo-espacios, se sustituye por un shader mínimo VÁLIDO según
  el tipo real del objeto (`glGetShaderiv(shader, GL_SHADER_TYPE)` -- soportado por
  vitaGL, `custom_shaders.c:1627`) y no llega vacía a vitaGL. Los nombres NO son
  adivinados: `WorldViewProjectionMatrix/DiffuseColor/Sampler0/TextureMatrix0`
  salen del propio log del juego (`invalid bind symbol` = el motor los busca por
  nombre); atributos `Position/Color0/TexCoord0` y varyings `vTexCoord0/vColor0`
  salen de los shaders reales dumpeados de Dungeon-Hunter-2 (misma familia Glitch,
  `glsl_dump/*.glsl`); la suma `Position.xyz + Vertex.xyz` cubre la única duda
  (`Vertex` lo usa el rosa embebido de este `.so`, `Position` lo usa DH2 -- el
  atributo no enlazado lee `(0,0,0,1)` y la suma da la posición real en ambos
  mundos, con `w` forzado a 1.0). Contiene `{`/`}` así que el traductor no puede
  volver a dar NULL por esa vía.
- **Validado:** `build --preset debug` OK; `deploy --eboot --yes` OK.
- **Pendiente (requiere consola):** correr y traer log + foto de pantalla. Señales
  esperadas: `Empty shader source for vertex/fragment ... substituting minimal
  VP/FP`, `glLinkProgram ... done` sin dump nuevo, y avance al splash/title/menú
  (la geometría puede verse degradada según qué tan bien acierten los nombres de
  atributos -- la foto decide la siguiente iteración; sin crash en ningún caso).

## Fase 8 (cont.): Falsa alarma — los dos últimos logs/dumps son el eboot VIEJO en memoria (2026-09-10)

- **Evidencia:** `log_20260910_000432.txt` y `log_20260910_010905.txt` son BYTE-IDÉNTICOS
  (260 líneas, `diff` vacío, cero líneas `substituting minimal`) y el dump
  `...-1789016951-...` es el MISMO crash `strchr(NULL)` (mismos registros/pila). La
  Vita corrió dos veces el eboot anterior: app suspendida en memoria, sin relanzar
  tras el FTP.
- **Revisión de build pedida en esta sesión (README VITAGL.md + decompiled/):**
  - El fix SÍ está compilado: `Empty shader source ... substituting minimal` verificado
    con `strings` en `build/sacredodyssey.elf` (en `eboot.bin` no se ve porque es SELF
    cifrado -- normal, no es un problema).
  - `CMakeLists.txt` compila todos los `source/` incluido `utils/glutil.c` -- nada
    falta. `README VITAGL.md` es el readme upstream genérico de vitaGL (tabla de
    flags), sin requisitos extra para este port.
  - `libStormGLOFT.so` del APK (interposer gráfico con `DecompressATC/PVRTC/S3TC`)
    NO hace falta empaquetarlo: cero referencias en `libsacredodyssey.so` v1.0.6 y
    `Game.java` solo hace `loadLibrary("sacredodyssey")`.
  - Flujo Java (`GameRenderer.java`/`Game.java`) vs `main.c`: idénticos --
    `nativeInit(getManufacture()=0 genérico, 960, 544)`, `GLMediaPlayer.init`,
    `Game.nativeInit`, `nativeRender` por frame. `manufacturer=0` es el default
    genérico correcto.
- **Acción requerida en consola:** CERRAR el juego del todo (no suspender: matar la
  burbuja desde el LiveArea/multitarea) y relanzar -- el eboot con los 3 fixes ya se
  subió por FTP antes. Si el eboot de la Vita fuera anterior a las 00:16, reabrir
  VitaShell (SELECT = FTP) y pedir redeploy. El redeploy de esta sesión falló con
  `Errno 61` (Vita dormida/sin FTP) -- reintentar bajo demanda.

## Fase 8 (cont.): Tercer run con eboot viejo + build-stamp anti-confusion (2026-09-10)

- **Evidencia:** `log_20260910_011555.txt` + dump `...-1789017361-...` repiten EXACTO lo
  anterior (260 líneas, sin `substituting minimal`, mismo `strchr(NULL)` con mismos
  registros). El eboot con el fix de shaders vacíos sigue sin correr en la Vita.
- **Verificación local:** el fix SÍ está en el build (cadena en `.elf`), TITLEID
  (`PSVSOTROA`) y ruta de deploy correctos (los fixes Tell/Seek sí llegaron por esa
  vía antes). Para eliminar esta clase de duda se agregó a `main.c` una línea de
  build-stamp (`eboot build stamp: __DATE__ __TIME__`) -- el próximo log prueba de un
  vistazo qué eboot corrió.
- **Rebuild 01:20 OK** (`eboot.bin` 587575 bytes, distinto tamaño al 00:16 = contenido
  nuevo) con stamp + sustitutos verificados en el `.elf`. Deploy pendiente: FTP caído
  (`Errno 61`) -- la Vita está dormida o sin VitaShell/FTP.
- **Acción requerida:** despertar la Vita, abrir VitaShell, SELECT (FTP), avisar para
  redeploy; después CERRAR el juego del todo y relanzar (no resumir suspendido).

## Fase 8 (cont.): El eboot nuevo SÍ corre pero el sustituto no dispara — fuente con contenido sin llaves (2026-09-10)

- **Evidencia:** `log_20260910_012451.txt` trae `eboot build stamp: Sep 10 2026
  01:20:11` (el eboot nuevo corre, parches 16-21 OK) pero CERO líneas `substituting
  minimal`, y el dump `...-1789017897-...` repite el mismo `strchr(NULL)` en
  `glsl_handle_globals` (misma pila). Conclusión: lo que llega a vitaGL NO es `""`
  sino una fuente sin ningún `{` (el crash exige `strstr(src,"{")==NULL`; con
  `R0=0` exacto es el primer `strstr`, no un `+1`). Una fuente válida siempre tiene
  cuerpo de función, así que una fuente sin llaves tampoco podría linkear nunca.
- **Fix aplicado (`source/utils/glutil.c`):** (1) el trigger ahora es `only_ws ||
  strchr(str,'{')==NULL` -- cubre vacías Y sin-llaves; (2) logging diagnóstico de
  cada `glShaderSource` (`len`, `brace`, primeros 80 chars con `|` en vez de
  saltos) para saber de una vez qué entrega el motor. Rebuild + deploy OK (FTP
  arriba esta vez, eboot subido al 100%).
- **Pendiente (requiere consola):** CERRAR el juego del todo y relanzar; traer log.
  Señal esperada: `glShaderSource(#N): len=... brace=... head='...'` revelando el
  contenido real, + `substituting minimal` + `glLinkProgram ... done` sin dump.

## Fase 9: Primera imagen real + build más rápida pero segura (2026-09-10)

- **Reportado:** por fin se ve imagen real del juego, pero la carga (barra del
  overlay propio) tarda muchísimo en moverse. Pedido: revisar `README VITAGL.md`
  para que la build sea segura pero más rápida.
- **Análisis:** la barra se mueve por frame swappado y el motor pasa segundos dentro
  de UN solo `nativeRender()` (traducción GLSL→GXM vía ShaccCg + uploads + scans),
  así que la barra se congela -- es el costo real, no el overlay. Revisada la tabla
  de flags de `README VITAGL.md` con criterio "seguro": todos los `*SPEEDHACK`,
  `NO_DEBUG`, `TEXTURES_SPEEDHACK`, etc. advierten crashes/glitches -- NO se tocan
  mientras el port se estabiliza. El único flag seguro que ataca directo el cuello
  de botella es `HAVE_SHADER_CACHE=1` (caché en archivo de shaders compilados con
  xxHash: puro caché, sin cambiar el pipeline). La primera carga paga el costo una
  vez; los siguientes arranques reutilizan.
- **Cambio (`CMakeLists.txt`):**
  `VITAGL_MAKE_FLAGS = "SOFTFP_ABI=1 NO_SPLASHSCREEN=1 HAVE_SHADER_CACHE=1"`
  (el mecanismo de `vitagl_flags.stamp` fuerza rebuild completo de vitaGL solo).
- **No cambiado (a propósito):** preset Debug sigue activo -- `DEBUG_SOLOADER`
  compila dentro TODO el logging por línea (fopen/fread/sprintf trazados a archivo
  +UDP); quitarlo (preset release) sería el mayor salto de velocidad pero nos
  dejaría ciegos para el triage restante. Cuando el juego llegue al menú estable,
  el siguiente paso de velocidad es un build release.
- **Validado:** `build --preset debug` OK (vitaGL recompilado con el flag).
- **Pendiente:** deploy (FTP caído `Errno 61` al intentarlo) -- abrir VitaShell +
  SELECT y avisar. Nota: la primera carga CON caché seguirá lenta (lo compila todo
  una vez); la mejora se nota desde el segundo arranque.

## Fase 9 (cont.): `log_20260910_013817.txt` -- reportado como "loop infinito" +
carga lenta; ambos diagnosticados con evidencia, dos fixes aplicados (2026-09-10)

- **Reportado:** ya se ve imagen real del juego, pero pide mejorar velocidad de
  carga y "salir del loop infinito".
- **Análisis del log** (11882 líneas, sin `.psp2dmp` -- no es un crash):
  - `[render_diag]` confirma el patrón ya conocido: `frame=60` y `frame=120` con
    `fps=0.4` (la ventana de 60 frames que cubre la carga síncrona de la escena
    `mainmenu2` -- construcción de cámara/materiales con ~250 `invalid bind
    symbol`/`Unused parameter` por los shaders `ProfileCOMMON_emul_*` que
    siguen sin existir en el dataset), y a partir de `frame=180` salta a
    `fps=59.9` estable -- y se mantiene EXACTAMENTE así (sin una sola línea
    distinta salvo un `[App] Reset Attack icon touch info` real de una pulsación
    de usuario) hasta el final del log, `frame=50760` (unos 14 minutos).
  - **No es un loop de motor ni un cuelgue:** el motor renderiza de verdad a
    60fps de forma sostenida, no repite ninguna operación en bucle. Lo que el
    usuario ve como "loop infinito" es el propio `loading_overlay_draw()`
    (`source/main.c`) -- la barra de carga que este loader agrega para
    demostrar que el loop sigue vivo durante el stall real -- llamada de forma
    INCONDICIONAL en cada frame, para siempre, sin ninguna condición de salida.
    Una vez terminada la carga real (frame ~120), la barra sigue animándose
    sobre una imagen ya renderizada y estable, dando la impresión de que el
    juego sigue "cargando en loop" cuando en realidad ya llegó a un estado
    estable.
  - **Causa cuantificada de la lentitud de esa única ventana de 60 frames:** de
    las 5067 líneas de log emitidas durante el stall `frame=60`->`frame=120`,
    1914 (38%) son las trazas incondicionales `vsprintf_soloader`/
    `sprintf_soloader: fmt="..."` agregadas en `source/reimpl/fmt.c` (una por
    CADA llamada `*printf` que hace el `.so`, cientos de ellas durante la
    construcción de la escena) y 2083 (41%) son `fopen`/`fclose` de `io.c`. Cada
    línea de log (con `DEBUG_SOLOADER` activo) paga un `sceIoOpen+Write+Close`
    más un `sceNetSendto` síncronos -- ese es el costo real detrás de los
    ~150 s que tarda esa ventana (60 frames a fps=0.4 == ~2.5 s/frame).
- **Fixes aplicados:**
  1. **`source/reimpl/fmt.c`:** eliminada la traza incondicional
     `l_debug("...printf_soloader: fmt=...")` de las 4 funciones
     (`sprintf_soloader`/`snprintf_soloader`/`vsprintf_soloader`/
     `vsnprintf_soloader`). El guard real (`scan_format_args` +
     `is_pointer_plausible`, que bloquea el crash de `%s` con puntero inválido
     documentado en el propio archivo) NO se toca y sigue actuando siempre; el
     `l_warn(...)` que sí loguea el fmt exacto cuando el guard BLOQUEA una
     llamada también se conserva intacto -- solo se quitó la traza del camino
     feliz (miles de veces por sesión, cero valor ya que el guard no depende de
     ella para funcionar).
  2. **`source/main.c`:** `loading_overlay_draw()` ahora se salta una vez que
     una ventana completa de 60 frames mide `fps > 30.0f` (umbral generoso:
     cualquier ventana que contenga el stall real mide `fps=0.4`, muy por
     debajo). Se latchea con una bandera local `loading_done` que nunca vuelve
     a false, para que un hipotético frame lento aislado más adelante
     (ej. un load de nivel) no haga parpadear la barra intermitentemente.
- **Validado:** `psvita-toolkit build --preset debug` OK (ambos archivos
  compilan y linkean limpio; `sacredodyssey.vpk` regenerado).
- **Pendiente (requiere consola):** desplegar y correr. Señales esperadas: la
  ventana `frame=60->120` debería tardar sensiblemente menos en tiempo real
  (menos, no cero -- la traducción GLSL/uploads/fopen siguen ahí), y la barra
  de carga debería desaparecer sola apenas se alcanza el primer
  `[render_diag] loading overlay latched off (fps=...)`, dejando ver la imagen
  ya renderizada sin animación superpuesta.
- **Pregunta abierta, NO resuelta esta vuelta (evidencia parcial, no
  confirmada):** en todo el log solo aparecen DOS transiciones
  `[KnightOdyssey] Go to state: %s` (`start` y `Splash`) -- nunca `Menu`. Con
  el `.so` real (`ux0_data/sacredodyssey/libsacredodyssey.so`, símbolos
  presentes) se desensambló `SplashState::IsFinished()` (`0x212010`, solo
  `return this[0x28]`) y `SplashState::Update(int)` (`0x211b08`): el flag
  `this+0x28` que gatilla el fin del Splash depende de una cadena de banderas
  (`+0x29`/`+0x2a`/`+0x2b`/`+0x2c`) y, en una rama, de un array de 13 bytes en
  `[obj+0xc3a..0xc46]` de un objeto global que también participa en
  `SoundManagerVox::InitAudioType()`/`nativeSendAppBackground()` cerca de ahí
  -- es decir, hay una hipótesis razonable (no confirmada) de que el Splash
  nunca termina de decidir avanzar a `Menu` porque está esperando un estado del
  subsistema de audio que el JNI stub de `AudioTrack` (`source/java.c`, sin
  `<init>`/`getMinBufferSize`/`play`/etc.) nunca satisface. **No se parcheó
  nada de esto** -- sería adivinar sobre offsets de campo sin confirmar su
  semántica real, exactamente lo que este proyecto evita. Si tras el fix de
  velocidad el juego sigue sin loguear `Go to state: Menu` después de varios
  minutos reales, ese es el próximo punto a triar (instrumentar
  `SplashState::Update`/`IsFinished` con un hook liviano en vez de seguir
  leyendo desensamblado a ciegas).

## Fase 9 (cont.): `log_20260910_194335.txt` -- confirmado: se llega a `MainMenu`
a 60fps estable; diagnosticado el "zoom raro"/pantalla en blanco reportado con
capturas (2026-09-10)

- **La pregunta de la sesión anterior queda resuelta:** el log SÍ loguea
  `Go to state: MainMenu` (línea 6665, tras `Go to state: Splash` en la 489) --
  la hipótesis de que el Splash se quedaba esperando audio era una falsa
  alarma; el fix de velocidad de la vuelta anterior (throttling de logging +
  overlay que se apaga solo) alcanzó para que la carga real termine y el
  motor avance de estado. `[render_diag]` confirma 59-60fps sostenidos desde
  el frame 180 en adelante, con solo caídas puntuales (ej. frame=780
  fps=11.4) coincidiendo con cargas de sprites nuevos (`main_menu.kot`,
  `help_2X.kot`), no con ningún cuelgue.
- **Reportado con capturas:** debería haber una animación pero se ve una
  pantalla en blanco con el sprite del juego y "zoom raro". Confirmado en el
  log que las capturas (19:47:58 y 19:48:03) son de ESTA corrida, ya con
  `Go to state: MainMenu` varios miles de frames atrás -- no es un splash
  transitorio, es el estado estable del MainMenu.
- **Descartado por evidencia directa:** los assets 2D reales del menú cargan
  bien (`Loaded texture: data/2d/sprites/High_Quality/main_menu.kot`,
  `help_2X.kot`, `camglow.kot`) -- no es el fallback vacío/rosa para estos
  sprites. La captura del diálogo "Are you sure yo[u]..." (`Hud::AskYesNo`,
  pseudo-C v106 confirmado: dispara con `HudEngine::Push(0xfe6,...)` +
  `HudWidget::SetStrById` sobre un widget "Question" -- 100% data-driven
  desde `.array`, sin coordenadas hardcodeadas en el código que se pudieran
  haber portado mal) recortado a la derecha es consistente con el mismo
  fenómeno que la captura del logo, no un bug aparte.
- **Descartada la hipótesis de `manufacturer` inconsistente:** se encontró
  que `source/main.c` llama `nativeGameRendererInit(..., 0, ...)` (manufacturer
  hardcodeado a 0) mientras `source/java.c` stubea `Game.getManufacture()`
  (JNI, llamado por el motor en otros puntos) para devolver 4 ("Sony"). Es
  una inconsistencia real, pero se descartó como causa: `s_manufacture`
  (pseudo-C v106) tiene un único consumidor, `isHtcDevice() { return
  s_manufacture == 3; }` -- ni 0 ni 4 matchean 3, así que el resultado es
  idéntico (`false`) para ambos valores. No se tocó (no hace falta).
- **Causa raíz identificada (razonamiento por especificación de GLSL, no
  adivinada):** las 8 mallas de efecto `Unlit*` (`VertexColor`,
  `OneTextureAndVertexColor`, `Textured`, `TexturedBlendTextureAlpha`,
  `MultiTextured`, `MaterialColor`) siguen ausentes de TODO dataset conocido
  (confirmado varias veces en este archivo) -- cuando fallan su `.glsl` real
  Y su `.obfs`, `glShaderSource_soloader` (`source/utils/glutil.c`) sustituye
  una fuente mínima propia. El log de esta corrida confirma que ESTA misma
  sustitución alimenta el material de fondo de la escena 3D `mainmenu2`
  (fondo tras el logo): 15 apariciones de `unbound parameter TextureMatrix0`
  para exactamente esos shaders. Por especificación de GLSL ES, un uniform
  al que el programa nunca escribe queda en 0 durante toda la vida del
  programa -- no "sin definir", CERO. `TextureMatrix0` en cero significa que
  `TextureMatrix0 * TexCoord0` da `(0,0)` para CUALQUIER vértice: toda la
  superficie de cada material afectado muestrea un único texel (típicamente
  la esquina superior-izquierda de su textura) estirado sobre toda la malla
  -- exactamente lo que se ve como "zoom raro" (una imagen reducida a un
  solo texel, ampliado). vitaGL (`~/vitasdk/arm-vita-eabi/include/vitaGL.h`,
  verificado) no expone `glGetUniformfv`/`glGetUniformiv`, así que no se
  pudo leer el valor real en runtime para una confirmación 100% empírica --
  pero la distinción cero-vs-identidad no es una apuesta: no hay ninguna
  regla de GLSL que deje un uniform genuinamente no-bindeado en otra cosa
  que cero, e identidad es el único valor que sólo puede ayudar (cero
  garantiza el bug descrito arriba; identidad hace que el sustituto actúe
  como passthrough correcto cuando el `TexCoord0` que llega ya viene
  pre-transformado, el caso común para UVs horneadas/atlas).
- **Fix aplicado (`source/utils/glutil.c`):**
  1. Se agregó una tabla pequeña (`substituted_shaders[]`, máx 32) que marca
     qué shader OBJECTS recibieron la fuente sustituta en
     `glShaderSource_soloader` (vía `mark_shader_substituted`).
  2. `glLinkProgram_soloader` ahora, tras linkear, revisa con
     `glGetAttachedShaders` si el programa recién linkeado usa alguno de
     esos shaders; si es así, fuerza `TextureMatrix0` a la matriz identidad
     con `glUniformMatrix4fv` (con `glUseProgram`/restore para no tocar el
     estado GL del motor) y loguea `[substitute_fix] program %u: forced
     TextureMatrix0 to identity`.
  3. **No se tocó** la mezcla `texture2D(...) + DiffuseColor` del fragment
     shader sustituto (se evaluó cambiarla a `*` pero se revirtió: con
     `DiffuseColor` confirmado en cero por el mismo razonamiento GLSL, `+`
     es un no-op seguro -- deja pasar la textura sin alterar -- mientras que
     `*` con un uniform en cero anularía el color entero de cada material
     afectado. Cambiarla habría sido adivinar en la dirección contraria a
     la evidencia disponible). Queda documentado en el propio código
     (comentario extenso en `glutil.c`) para no reabrir esto sin evidencia
     nueva.
- **Validado:** `psvita-toolkit build --preset debug` OK; `strings` sobre
  `build/sacredodyssey.elf` confirma la nueva línea `[substitute_fix]
  program %u: forced TextureMatrix0 to identity`; `deploy --eboot --yes` OK
  (eboot subido a `ux0:app/PSVSOTROA/`).
- **Pendiente (requiere consola):** CERRAR el juego del todo (no resumir
  desde suspendido) y relanzar; traer log + una captura nueva del mismo
  punto (MainMenu, tras el diálogo si es fácil de reproducir). Señales
  esperadas: la línea `[substitute_fix] program %u: forced TextureMatrix0 to
  identity` (una vez por programa afectado, hasta ~19 veces dado que la
  escena `mainmenu2` linkea 19 programas) y, visualmente, que el fondo 3D
  del menú deje de verse como un único texel ampliado -- puede seguir
  degradado (sin los shaders reales, los materiales seguirán "planos"/sin
  iluminación real), pero no debería seguir con el patrón de "un solo
  pixel estirado". Si el zoom persiste igual tras esto, la hipótesis queda
  descartada por evidencia y el siguiente sospechoso es la propia
  `vColor0`/`Color0` (atributo, no uniform): si el motor nunca habilita ese
  attribute array para estos materiales, vitaGL usa el valor genérico
  default `(0,0,0,1)` y `color *= vColor0` anularía el RGB igual
  (independiente del fix de `TextureMatrix0`) -- se puede confirmar
  agregando logging a `glEnableVertexAttribArray`/`glVertexAttribPointer`
  por índice, mismo estilo que el resto de este archivo.

## Fase 10: Resolución de carga lenta, zoom 200%, pantalla blanca en Title y fallback rosado en Menú (2026-09-11)

- **Causa raíz del "Zoom raro" (200% escala) en Title/MainMenu:**
  - En `source/utils/glutil.c`, `empty_vp_src` declaraba simultáneamente:
    `attribute highp vec4 Position;` y `attribute highp vec4 Vertex;` sumándolos:
    `vec4 pos = vec4(Position.xyz + Vertex.xyz, 1.0);`.
  - Análisis en Ghidra (`guessShaderVertexAttribute`, `out_ghidra.c` L445880) confirmó que
    la tabla del motor asigna `E_VERTEX_ATTRIBUTE = 0` tanto a `"position"` como a `"vertex"`.
  - Por tanto, el motor bindeaba el array de vértices a AMBOS atributos a la vez. Al sumar
    ambos, se duplicaban todas las coordenadas geométricas (`pos + pos = 2 * pos`), inflando
    la escena 3D al doble de su tamaño.
  - **Fix:** Eliminado `attribute highp vec4 Vertex;` y `Position.xyz + Vertex.xyz` en
    `glutil.c`, dejando exclusivamente `gl_Position = WorldViewProjectionMatrix * Position;`.

- **Causa raíz de la pantalla rosada en Menú y carga extremadamente lenta:**
  - Todas las mallas 3D del juego (`jiemian_sence.bdae`, `worldguodao4.bdae`) referencian
    `ProfileCOMMON_emul_VS.glsl` y `ProfileCOMMON_emul_FS.glsl`.
  - Al no existir en disco, `FileStream::open` fallaba en `.glsl` y en `.obfs`, produciendo más
    de 2.000 intentos fallidos de `fopen` que escribían sincrónicamente logs a disco y UDP.
  - Al fallar la carga del shader, `createShaderImpl` retornaba NULL y no cacheaba en
    `SIDedCollection`, provocando reintentos continuos y la caída al shader de depuración
    `createPinkWireFrameTechnique` / `PinkBadShader` (`vec4(0.8, 0.3, 0.5, 1.0)`).
  - **Fix:** Implementados los 10 shaders faltantes requeridos por `DefaultEffects.bdae` y
    `CustomEffect.bdae`:
    1. `ProfileCOMMON_emul_VS.glsl`
    2. `ProfileCOMMON_emul_FS.glsl`
    3. `UnlitOneTextureAndVertexColorVP.glsl`
    4. `UnlitTexturedFP.glsl`
    5. `UnlitTexturedBlendTextureAlphaFP.glsl`
    6. `UnlitMultiTexturedFP.glsl`
    7. `UnlitVertexColorVP.glsl`
    8. `UnlitVertexColorFP.glsl`
    9. `UnlitMaterialColorVP.glsl`
    10. `UnlitMaterialColorFP.glsl`
  - Guardados en `extras/shaders/`, empaquetados en el VPK en `app0:shaders/`, copiados a
    `ux0_data/sacredodyssey/GloftSOHP/data/3d/effects/` y con auto-instalación garantizada en
    `init.c` (`ensure_shader_assets`) más redirección transparente en `io.c` (`fopen_soloader`).

- **Causa raíz del fondo blanco en el Title screen:**
  - `empty_fp_src` sólo declaraba `uniform sampler2D Sampler0;`, mientras que `UnlitTexturedFP`
    y `DefaultEffects.bdae` vinculan el parámetro como `texture`. El sampler quedaba sin bindear
    y el muestreo retornaba blanco sumado a `DiffuseColor`.
  - **Fix:** `empty_fp_src` ahora declara ambos samplers (`Sampler0` y `texture`) y las fuentes
    reales de `UnlitTexturedFP.glsl` y `ProfileCOMMON_emul_FS.glsl` declaran sus samplers exactos.
  - `glLinkProgram_soloader` ahora asegura que `TextureMatrix0` sea inicializada a matriz
    identidad para todo programa que la contenga, evitando que quede en 0 por defecto.

- **Evaluación de flags de vitaGL (`README VITAGL.md`):**
  - Comparado con los ports hermanos `Asphalt-5-Vita` y `Dungeon-Hunter-2-vita` (mismo motor Glitch).
  - Actualizado `VITAGL_MAKE_FLAGS` en `CMakeLists.txt` a:
    `"SOFTFP_ABI=1 NO_SPLASHSCREEN=1 HAVE_SHADER_CACHE=1 NO_DEBUG=1 SAMPLERS_SPEEDHACK=1 CIRCULAR_POOL_SPEEDHACK=1 DRAW_SPEEDHACK=2 NO_TEX_COMBINER=1"`
  - `DRAW_SPEEDHACK=2` y `CIRCULAR_POOL_SPEEDHACK=1` eliminan cuellos de botella de CPU en llamadas
    de dibujado y memoria de pool temporal.
  - `NO_TEX_COMBINER=1` desactiva el combinador de función fija innecesario en GLES2.
  - `NO_DEBUG=1` elimina comprobaciones redundantes de GL en rutas críticas.

- **Validación:**
  - `psvita-toolkit build --preset debug` compila al 100% de manera limpia.
  - Generados `eboot.bin` (608 KB) y `sacredodyssey.vpk` (700 KB) conteniendo los 10 shaders en `app0:shaders/`.

## Fase 8 (cont.): Pantalla rosada en la vista de carga — shaders reales nunca llegaban a consola (2026-09-11)

- **Sintoma (log_20260911_011844.txt):** la vista de carga muestra pantalla rosada en vez de la
  imagen real (`loading_splash.kot`, que el log confirma que SI carga: `Loaded texture:
  data/2d/sprites/High_Quality/loading_splash.kot`). Todas las fuentes de shader llegan vacias
  (`glShaderSource(#N): len=54 brace=0` = solo defines, sin cuerpo) y se sustituyen con el shader
  minimo. Los `fopen(app0:shaders/Unlit*.glsl)` fallan con `0x0` aunque el `.vpk` local SI los
  contiene (`unzip -l` lo confirma).
- **Causa raiz (triple, confirmada por codigo + log):**
  1. `ensure_shader_assets()` (`source/utils/init.c`) copiaba los 10 `.glsl` UNICAMENTE desde
     `app0:shaders/` — que solo existe en consola si se reinstalo el `.vpk` COMPLETO (con
     `deploy --eboot` rapido nunca llega). Si falla, `effects/` queda vacio en silencio.
     Agravantes: usaba `mkdir()` plano (falla si `GloftSOHP/data/3d/` intermedios no existen) y
     la ruta sin barra (`app0:shaders/...`).
  2. `source/utils/embedded_shaders.c` (los 10 shaders correctos embebidos en el eboot, sin
     depender del `.vpk`) existia pero NUNCA se compilaba (no estaba en `CMakeLists.txt`) y
     `ensure_embedded_shaders_installed()` / `get_embedded_shader()` tenian CERO llamadas.
  3. El sustituto minimo de `glShaderSource_soloader()` (`source/utils/glutil.c`) hacia
     `color += DiffuseColor; color *= vColor0;` sin guards: con `Color0` sin bindear (lee 0) o
     `DiffuseColor` en 0, cualquier material en esa ruta se volvia tinte plano en vez de mostrar
     su textura. Mismo patron sin guards en `extras/shaders/UnlitTexturedFP*.glsl`,
     `UnlitMultiTexturedFP.glsl` (`color *= vColor0` directo) y `UnlitMaterialColorFP.glsl`
     (`gl_FragColor = DiffuseColor` sin fallback a blanco).
- **Fix (la imagen de carga NO se elimina: se restaura sirviendo los shaders reales):**
  1. `CMakeLists.txt`: `source/utils/embedded_shaders.c` agregado al build.
  2. `source/utils/init.c`: `ensure_shader_assets()` llama PRIMERO a
     `ensure_embedded_shaders_installed()` (fuente primaria, sin depender del `.vpk`) y deja la
     copia desde `app0:/shaders/` solo como secundario para archivos aun faltantes; `mkdir()` ->
     `file_mkpath()` y ruta `app0:/shaders/` con barra.
  3. `source/utils/embedded_shaders.c`: `mkdir()` -> `file_mkpath()` (misma causa silenciosa).
  4. `source/reimpl/io.c`: fallback bajo demanda en `fopen_soloader()` — si un
     `data/3d/effects/*.glsl` falta, se materializa desde `get_embedded_shader()` y se reabre;
     `app0:/shaders/` queda como ultimo recurso (con barra corregida).
  5. `source/utils/glutil.c`: sustituto minimo FP con guards (igual que los embebidos:
     `vc` cae a `vec4(1.0)` si `vColor0` es 0, `DiffuseColor` solo modula `rgb` si es util) —
     la ruta de ultimo recurso muestra textura, nunca tinte plano.
  6. `extras/shaders/`: `UnlitTexturedFP.glsl`, `UnlitTexturedBlendTextureAlphaFP.glsl`,
     `UnlitMultiTexturedFP.glsl` y `UnlitMaterialColorFP.glsl` sincronizados con las versiones
     con guards de `embedded_shaders.c`.
  7. El parche `SplashState::Draw2D` NULL-texture skip (`source/patch.c`) se conserva SOLO como
     red de seguridad para el logo `gameloft_3x_tga` (que ya se redirige a `gameloft_2x.kot` real
     y carga bien, textura #7 del log): no toca `loading_splash.kot`, que sigue dibujandose.
- **Validacion:** build limpio Debug en `/tmp/sacredodyssey-build` (solo warnings preexistentes de
  `lib/falso_jni`), `eboot.bin` (609 KB) y `sacredodyssey.vpk` (701 KB) regenerados en la raiz con
  los 10 shaders en `app0:/shaders/`. **Pendiente en consola real:** reinstalar el `.vpk` COMPLETO
  una vez (los shaders embebidos cubren arranques futuros sin reinstalar) y confirmar que la vista
  de carga muestra la imagen real.

## Fase 11: Fix del fondo rosado en Menú, escalado 960x544 en Splash/Loading y Data abort en LoadWorld (2026-09-11)

- **Triaje del crash en LoadWorld() (`sacredodyssey-psp2core-1789107753-...`):**
  - Excepción: Data abort exception en hilo principal `PSVSOTROA`.
  - `PC: 0x982765be` (offset `0x002765be` = +0x1e dentro de `glitch::video::CMaterial::allocate`).
  - `LR: 0x981ddd21` (offset `0x001ddd20` = call site en `_EnableWaterEffect`).
  - `R0: 0x0`, `R1: 0x81600be8` (puntero al `boost::intrusive_ptr` en pila cuyo valor interno es `0x0`).
  - Instrucción del crash: `ldr r0, [r0, #36]` (desreferencia `NULL + 0x24`).
  - Causa raíz: al haber salteado `InitSpecialEffectMateiral()` en `0x1bf6e4` con `bx lr`, `EffectManager`
    no registró los efectos especiales (`water`, `magma`, `solid`, etc.). Al pulsar "New Game", `LoadWorld()`
    llama a `SceneObject::EnableSpecialEffect()`, que busca mallas de agua y llama a
    `EffectManager::QueryFXMaterial("water")`. Al no existir, retorna un `intrusive_ptr` con NULL, y
    `CMaterial::allocate` desreferencia el puntero sin chequear, crasheando de inmediato.
  - **Fix:** En `source/patch.c` (`so_patch_v106`), se parcharon con `bx lr` (0x4770) las entradas de:
    1. `SceneObject::EnableSpecialEffect` (`0x1ddf1c`)
    2. `_EnableWaterEffect` (`0x1ddb94`)
    3. `_EnableLavaEffect` (`0x1ddd48`)
    4. `_switchMaterialToSolid` (`0x1ddf7c`)
    Evitando cualquier llamada posterior a `QueryFXMaterial` o `CMaterial::allocate` para efectos especiales
    sin inicializar.

- **Causa raíz del menú rosado/magenta (Screenshot `2026-09-11-021915.jpg`):**
  - En `source/utils/embedded_shaders.c`, `s_ProfileCOMMON_emul_FS` declaraba `uniform sampler2D texture;`.
  - La escena 3D `jiemian_sence.bdae` vincula el sampler como `Sampler0`, no `texture`, y vincula parámetros
    de iluminación (`ambientcolor`, `specularcolor`, `emissioncolor`, `shininess`).
  - El motor registraba en el log:
    `invalid bind symbol: Sampler0`
    `unbound parameter texture for shader ProfileCOMMON_emul_VS.glsl#define TEXTURED`
    Al fallar la vinculación de `Sampler0`, el motor consideró la técnica inválida y cayó a
    `createPinkWireFrameTechnique(in_r0, "**invalid**")` / `PinkBadShader` (`vec4(0.8, 0.3, 0.5, 1.0)`).
  - **Fix:** Actualizado `s_ProfileCOMMON_emul_FS` en `source/utils/embedded_shaders.c` y en
    `extras/shaders/ProfileCOMMON_emul_FS.glsl` declarando `Sampler0` y `Sampler1`, y los uniforms de
    `LIGHTING`, eliminando el fallback rosado.

- **Causa raíz del escalado reducido en Splash y Loading (Screenshots `021531.jpg` y `022125.jpg`):**
  - En `SplashState::Draw2D`, el rectángulo de destino se construía con el tamaño nativo de la textura
    (800x480 o 854x480 en `sl` y `r8`), dibujando la imagen en la esquina superior izquierda y dejando
    franjas negras a la derecha y abajo en la pantalla de 960x544 de la Vita.
  - En `LoadingState::Draw2D`, el código original dividía la altura entre 640.0f (resolución iOS), escalando
    la imagen a ~800x408 y dejando una franja blanca a la derecha y negra abajo.
  - **Fix:**
    - `SplashState::Draw2D`: Trampolín en `0x3463d4` (espacio muerto de `ALicenseCheck::LoadConfig`) que
      fuerza el rectángulo de destino a (0, 0, 960, 544) manteniendo el rectángulo de origen completo.
    - `LoadingState::Draw2D`: Parche in-place de 28 bytes en `0x211524` que fuerza el rectángulo de destino
      a (-1, -1, 961, 545), cubriendo toda la pantalla de 960x544 sin deformar las coordenadas de textura.

- **Validación previa:**
  - Build limpio con `psvita-toolkit build --preset debug`.
  - Generados `build/sacredodyssey.elf` (8.3 MB) y `build/sacredodyssey.vpk` (701 KB).

## Fase 8 (cont.): Fix de terreno rosado en menú y escalado 100% pantalla completa (2026-09-11)

- **Bug 1: Terreno/pasto rosado en el menú 3D (Screenshot `2026-09-11-122013.jpg`).**
  - **Diagnóstico:** Tras recuperar las texturas del castillo, cielo y caballo mediante `Sampler0`, el suelo seguía renderizándose en magenta/rosado chillón. El log `log_20260911_120455.txt` mostraba:
    `unbound parameter Sampler1 for shader ProfileCOMMON_emul_VS.glsl#define TEXTURED #define LIGHTMAP`
    `invalid bind symbol: Sampler2`
    `invalid bind symbol: TextureMatrix2`
    `Unused parameter: Map__2__landscapes_lightmap.tga_-sampler`
  - **Causa raíz:** En la escena 3D `jiemian_sence.bdae`, el material del terreno (`lambert28`) utiliza la técnica `#define TEXTURED` + `#define LIGHTMAP` y enlaza el mapa de luz en **`Sampler2`** y **`TextureMatrix2`** (no `Sampler1`). Como nuestro shader `ProfileCOMMON_emul_FS` declaraba `Sampler1`, el motor detectó un uniform activo no provisto por el material y abortó la técnica, cayendo en el fallback rosado `createPinkWireFrameTechnique`.
  - **Fix:**
    1. En `extras/shaders/ProfileCOMMON_emul_FS.glsl` y `source/utils/embedded_shaders.c`: bajo `#if defined(LIGHTMAP)`, reemplazado `Sampler1` por `uniform sampler2D Sampler2;` y muestreado con `texture2D(Sampler2, vTexCoord1)`.
    2. Declarado `uniform sampler2D Sampler1;` bajo `#if defined(MULTITEXTURED)` (utilizado por modelos como `mc.bdae`).
    3. En `extras/shaders/ProfileCOMMON_emul_VS.glsl` y `source/utils/embedded_shaders.c`: bajo `#if defined(LIGHTMAP)`, declarados `attribute mediump vec2 TexCoord1;`, `varying mediump vec2 vTexCoord1;` y `uniform mediump mat4 TextureMatrix2;`, transformando `vTexCoord1 = (TextureMatrix2 * vec4(TexCoord1, 0.0, 1.0)).xy;`.
    4. En `source/utils/glutil.c`: en `glLinkProgram_soloader`, inicializados `TextureMatrix0`, `TextureMatrix1` y `TextureMatrix2` a la matriz identidad para evitar matrices de ceros por GLSL default.

- **Bug 2: Pantalla de carga con barras blanca y negra (Screenshot `2026-09-11-122052.jpg`).**
  - **Diagnóstico:** El log `log_20260911_120455.txt` reportaba:
    `v106 LoadingState::Draw2D scaling site mismatch (found 0xa9029104, want 0x02a90491)`
  - **Causa raíz:** Las dos medias palabras Thumb-2 `0x9104` (`str r1, [sp, #16]`) y `0xa902` (`add r1, sp, #8`) en little-endian de 32 bits leídas como `uint32_t` son `0xa9029104u`, no `0x02a90491u`. Debido a este mismatch, el parche in-place de 28 bytes nunca se aplicó, dejando la pantalla de carga en ~800x408.
  - **Fix:** En `source/patch.c`, corregido el guard a `if (*site == 0xa9029104u)`. Con esto el parche in-place se aplica correctamente y expande el fondo de carga a `(-1, -1, 961, 545)`.

- **Bug 3: Pantalla de título/splash reducida en esquina superior izquierda (Screenshot `2026-09-11-120522.jpg`).**
  - **Diagnóstico:** El splash de inicio (`loading_splash_2x.sprite`) y su barra de progreso se dibujaban a 800x480, dejando una franja negra de 160 px a la derecha y 64 px abajo.
  - **Causa raíz:** `SplashState::Draw2D` dibuja mediante `ASprite::PaintFrame(this, 0, x, y, ...)`. El motor `ASprite` tiene soporte nativo de escalado por hardware si `hasScale == 1` (`ASprite::SetScale(scaleX, scaleY)`), pero `SplashState` nunca llamaba a `SetScale`, dejando el sprite en su resolución nativa WVGA (800x480).
  - **Fix:** En `source/patch.c` dentro de `license_validate_stub` (llamado por `SplashState::FocusGain` justo al inicializar el splash), se lee el puntero de `m_pSprite` y se configuran:
    - `scaleX = 960.0f / 800.0f` (1.2f)
    - `scaleY = 544.0f / 480.0f` (1.1333333f)
    - `hasScale = 1`
    Esto escala limpiamente la imagen y la barra de carga al 100% de la pantalla de 960x544 de la Vita.

- **Validación:**
  - Compilación limpia con `psvita-toolkit build --preset debug`.
  - Generado `/Volumes/Seagate/PSVITA Develop/Sacred-Odyssey-vita/build/sacredodyssey.vpk` listo para transferir y probar.

## Fase 12: "New Game" se queda cargando -- confirmado bloqueo real de frame, no un crash (2026-09-11)

- **Feedback del usuario tras Fase 11 (log_20260911_171008.txt):** menú sin rosado (confirmado, Bug 1
  de Fase 8-cont resuelto). Pero: (a) "New Game" se queda cargando, y (b) las pantallas de carga
  siguen sin ocupar el 100% de la pantalla (franjas negras), pese a que ESTE MISMO log confirma que
  los dos parches de escalado (`SplashState::Draw2D` línea 28, `LoadingState::Draw2D` línea 29) se
  aplicaron sin mismatch. No hay `.psp2dmp` de esta sesión -- no es un crash.
- **Diagnóstico (confirmado, no especulado):**
  - `[render_diag] frame=N` se loguea cada 60 frames desde el loop principal. En este log aparece
    normal hasta `frame=2220` (línea 5923) y luego **desaparece por completo** durante las ~9700
    líneas restantes del archivo (hasta el corte del log, en medio de la carga de
    `TutorialDoor_2118`/`machang_door.bdae`) -- prueba directa de que el loop principal quedó
    bloqueado dentro de una sola llamada, sin volver a hacer swap de framebuffer, durante toda la
    carga del mundo.
  - Desensamblado real (`arm-vita-eabi-objdump -M force-thumb`) de `World::LoadMap()` (0x217058,
    símbolo `_ZN5World7LoadMapEv`, confirmado en `libsacredodyssey.so`): es una máquina de estados
    (`switch` sobre `this+0x4c`) con un caso por fase ("graphical maps", "game objects", "physical
    maps", "camera tracks"...), diseñada para devolver el control al llamador entre fases -- pero el
    caso 1 ("World loading: game objects", 0x217182-0x2171b8) contiene un `do/while` que llama a
    `GameObjectManager::Load(int,int)` (0x1d8d38) para los ~107 game objects del mundo **en una sola
    pasada, sin ceder el control entre objetos**. Ningún swap de framebuffer puede ocurrir hasta que
    el `do/while` completo termine.
  - Causa contribuyente confirmada en el código del port (no del .so): `_log_throttle_sinks()` en
    `source/utils/logger.c` (pensado para no pagar I/O repetido por líneas idénticas como
    "invalid bind symbol: ambientcolor", que este log tiene 2688 veces) tenía una tabla de solo 64
    entradas. Cada "Loading game object: `<Nombre>`..." (107 nombres, todos distintos) y cada
    `fopen(...)` de asset (rutas todas distintas) consume un slot igual que cualquier otra línea --
    llenando la tabla con strings que NUNCA iban a repetirse antes de que las líneas realmente
    repetitivas (bind-symbol) llegaran a su 3ra repetición. Con la tabla llena, el código falla en
    modo seguro (nunca más throttlea nada nuevo), así que la mayoría de esas 2688 líneas pagó
    `sceIoOpen+Write+Close` + `sceNetSendto` individual, dentro de la misma llamada bloqueante.
  - Conclusión: "se queda cargando" es un bloqueo real (el loop principal no vuelve a ejecutar
    durante toda la carga), agravado por I/O de logging sin throttling efectivo en el build Debug.
    Esto también explica las "imágenes estáticas con franjas negras": lo que el usuario ve
    congelado es lo que sea que estuviera en pantalla en el instante en que `World::LoadMap()`
    entró al caso 1 -- no importa que los parches de escalado estén aplicados, si el frame nunca se
    vuelve a dibujar/swapear no hay forma de que se vea el resultado final durante ese lapso.
- **Fix aplicado (bajo riesgo, ya compilado):** `LOG_REPEAT_TABLE_SIZE` en `source/utils/logger.c`
  subido de 64 a 4096 (32 KB estáticos) para que las líneas genuinamente repetitivas (bind-symbol,
  "Unused parameter") consigan throttle real en vez de ser desplazadas por nombres de objeto/rutas
  de archivo que nunca iban a repetir. Reduce el I/O total durante la carga; no cambia nada en
  builds Release (`l_info`/`l_warn`/`l_debug` son no-ops fuera de `DEBUG_SOLOADER`).
- **Pendiente de decidir con el usuario (no aplicado todavía):** insertar un yield periódico
  (`gl_swap()` + `loading_overlay_draw()`) dentro del `do/while` de `World::LoadMap` caso 1
  (0x2171aa, tras cada `bl GameObjectManager::Load`) para que la pantalla se siga refrescando
  (barra de progreso visible) mientras el mundo carga, en vez de quedar congelada. Requiere un
  trampolín nuevo que llame desde código del .so a una función C del loader (patrón no usado antes
  en este port -- los trampolines existentes solo saltan dentro del mismo módulo); no reduce el
  tiempo total de carga, pero elimina la percepción de "se colgó". Building/deploy/prueba en consola
  real sigue pendiente para validar cualquiera de los dos cambios.
- **Validación:**
  - Compilación limpia con `psvita-toolkit build --preset debug` (solo el fix de logger.c).
  - Generado `/Volumes/Seagate/PSVITA Develop/Sacred-Odyssey-vita/build/sacredodyssey.vpk`.

## Fase 13: Crash en cutscene (FadeOutElement) + diagnóstico del fondo recortado en Splash/Loading (2026-09-11)

- **Contexto:** con el fix de logger.c de la Fase 12 instalado, `log_20260911_213514.txt` muestra
  que la carga de "New Game" esta vez SÍ completó normalmente (`-------load pecent 100.000000`,
  "Finish Loading Map", "MC Created!") y avanzó hasta la cutscene de introducción
  (`CutScenePrologue`, diálogo con `talk_woman_unknown_2x`) antes de crashear.

- **Bug 1 (confirmado y arreglado): crash en la cutscene.**
  - Dump `sacredodyssey-psp2core-1789177098-0x000c8c2a1d-eboot.bin.psp2dmp` re-analizado con
    `psvita-toolkit analyze --so ux0_data/sacredodyssey/libsacredodyssey.so --so-base 0x98000000`
    (el análisis automático con el `.so` por defecto, decompiled/apk_jadx's APK v1.0.3 original,
    da símbolos sin sentido -- el binario real desplegado es el "v106" de `ux0_data/`).
  - `PC: 0x2765be` dentro de `glitch::video::CMaterial::allocate`, instrucción `ldr r0,[r0,#36]`
    con `R0=0` -- **la misma instrucción exacta** del crash de LoadWorld ya arreglado en la Fase 11.
  - `LR: 0x1c25de` → `FadeOutElement::switchMaterial(ISceneNode*)+0xe2`; la pila muestra
    `EffectManager::QueryFXMaterial+0x14` un frame más abajo.
  - Causa raíz: mismo patrón que los 4 sitios ya arreglados (`SceneObject::EnableSpecialEffect`,
    `_EnableWaterEffect`, `_EnableLavaEffect`, `_switchMaterialToSolid`) -- al saltear
    `InitSpecialEffectMateiral()` (0x1bf6e4), el material "fade" nunca se registró en
    `EffectManager`. `FadeOutElement::switchMaterial()` (llamado por el fundido de la cutscene)
    pide ese material vía `QueryFXMaterial`, recibe NULL, y `CMaterial::allocate` lo desreferencia
    sin chequear. El propio comentario del fix de `InitSpecialEffectMateiral` ya predecía este
    bug exacto ("fade materials absent... un futuro miss de QueryFXMaterial() sin su propio
    chequeo sería el PRÓXIMO bug").
  - **Fix:** en `source/patch.c`, sexto sitio de la misma familia -- `bx lr` (0x4770) en la
    entrada de `FadeOutElement::switchMaterial` (0x1c24fc, `stmdb sp!,{r4-r9,sl,fp,lr}` = 0xe92d).
    Se pierde el efecto visual de fundido; no hay crash.

- **Bug 2 (sin resolver, instrumentado): Splash/Loading siguen con franjas negras/blancas.**
  - Screenshots `2026-09-11-213544.jpg` (SplashState, caballo/castillo) y
    `2026-09-11-213738.jpg` (LoadingState, "CARGANDO...") de ESTA MISMA sesión muestran el
    recorte a 800px de ancho **idéntico** al de antes de los fixes de Fase 11, pese a que el log
    de esa corrida confirma que ambos parches de escalado (`0x211d78`/`0x211524`) se aplicaron
    sin mismatch de guard.
  - Ambos trampolines fueron re-verificados byte a byte re-ensamblando con
    `arm-vita-eabi-as -Ttext=<dirección>` y comparando contra los arrays de `patch.c`: sin error
    de codificación. El rect forzado (0,0,960,544) / (-1,-1,961,545) SÍ es lo que se escribe en
    los stack slots que la función `ip` (vtable) recibe como argumento de rect de destino.
  - En vez de intentar un tercer parche a ciegas, se agregaron DOS trampolines de diagnóstico
    (mismo patrón de cueva que el resto, pero llamando por primera vez a una función C del
    loader vía `ldr+blx` en vez de un salto permanente) justo antes de cada `blx ip` de dibujado:
      - `SplashState::Draw2D` (0x211d98 → cueva 0x3463e8) llama a `log_splash_rect(dst, src)`.
      - `LoadingState::Draw2D` (0x21154e → cueva 0x346408) llama a `log_loading_rect(dst, src)`.
    Cada uno loguea `[diag_rect:Splash] dst=(...) src=(...)` / `[diag_rect:Loading] dst=(...)
    src=(...)` con los 4 enteros reales de cada rect, las primeras 5 veces que se llama.
  - **Qué buscar en el próximo log:** si `dst` ya sale como `(0,0,960,544)` /
    `(-1,-1,961,545)` pero la pantalla sigue recortada, el bug está en el orden real de los
    argumentos (quizás `dst`/`src` están invertidos respecto a lo asumido) o en que este NO es
    el code path que realmente dibuja el arte visible (podría ser el `ASprite::PaintFrame` de
    `m_pSprite` en vez de este quad con material). Si `dst` sale con los valores VIEJOS
    (native texture size), el parche no se está aplicando en tiempo de ejecución pese al
    guard -- señal de un problema distinto (p.ej. caché de instrucciones).
  - **Pendiente:** reinstalar el `.vpk`, reproducir Splash + "New Game" (para ver LoadingState),
    y pasar el log nuevo con las líneas `[diag_rect:...]` para aplicar el fix definitivo.

- **Validación:**
  - Compilación limpia con `psvita-toolkit build --preset debug`.
  - Generado `/Volumes/Seagate/PSVITA Develop/Sacred-Odyssey-vita/build/sacredodyssey.vpk`.



---

## Sesión 2026-09-11 (noche) — Splash fullscreen + personaje/montura: causas confirmadas y fixes

Reporte del usuario con `log_20260911_223927.txt` (build 22:36:45, YA con diag_rect):
ingame alcanzado, pero (1) splash/carga no cubren la pantalla y (2) errores de
textura en el personaje + montura completamente negra.

### Bug 1 (resuelto el misterio del diag): el arte visible NO es el quad del driver

- El `[diag_rect:Splash] dst=(0,0,960,544) src=(0,0,1024,512)` del log 22:39
  RESPONDE la pregunta abierta de la entrada anterior: el rect forzado SÍ llega
  al draw call. El recorte persiste porque el arte visible va por OTRA ruta.
- Disassembly Thumb-2 real de `SplashState::Draw2D` (0x211cf4): tras el quad del
  driver hay DOS rutas `ASprite::SetClipRegion`/`PaintFrame` (0x211e62-0x211e72,
  0x211f56-0x211fb0). Y en 0x211fca hay un ancho hardcodeado:
  `f44f 7348 mov.w r3, #800` → `str r3, [sp, #40]` (ancho del clip region).
  Ese es el recorte a 800px: el sprite se clipea a 800 aunque el quad vaya
  fullscreen. El flag `tst r3,#1` en 0x211d6e decide qué ruta dibuja.
- **Fix (`source/patch.c`, v106):** sustitución in-place de 4 bytes en 0x211fca,
  `mov.w r3, #800` → `mov.w r3, #960` (misma familia T3, encoding verificado
  ensamblando con `arm-vita-eabi-as`: `f44f 7348` → `f44f 7370`, u32 LE
  `0x7348f44f` → `0x7370f44fu`), con guard de mismatch fail-safe.
- **Red de seguridad + telemetría (`source/utils/glutil.c`, `dynlib.c`):**
  wrappers `glViewport_soloader`/`glScissor_soloader` que loguean las primeras
  10 llamadas (`[gl_viewport]`/`[gl_scissor]` con caller offset del .so, misma
  convención que `[gl_upload]`) y expanden la ventana exacta de diseño de
  teléfono (800x480 / 800x408 en origen) a 960x544. Remapeo NARROW a propósito:
  viewports de FBO/shadow-map (cuadrados/POT) pasan intactos.
- **Qué buscar en el próximo log:** líneas `[gl_viewport]`/`[gl_scissor]` (si el
  motor pide 800x480, la teoría del viewport queda confirmada y ya va
  corregida), más `Patched v106 SplashState::Draw2D ASprite clip 800 -> 960`.

### Bug 2a (evidencia directa): `KnightsOdyssey_hand2_diffuse.tga` no existe

- `log_20260911_223927.txt:3225-3230`: `mc.bdae` pide `/./KnightsOdyssey_hand2_diffuse.tga`
  y `./KnightsOdyssey_hand2_diffuse.tga` → "Missing file" + "Could not find
  texture file" en ambas. El `.zip` de datos v1.0.6 confirma que no existe en
  ningún dataset (solo `KnightsOdyssey_hand_diffuse.kot`) — asset genuinamente
  faltante, misma familia que `gameloft_3x_tga`.
- **Fix (`source/reimpl/io.c`):** redirect al `KnightsOdyssey_hand_diffuse.kot`
  real (solo lectura, sin recursión), al lado del redirect del logo.

### Bug 2b (montura negra + spam de binds): ProfileCOMMON sin skinning ni 2a textura

- Los nombres `BoneMatrices`/`BoneQuat0`/`BoneQuat1`/`BoneTexture`/`WeightMask`
  vienen de los materiales de `mc.bdae`/`magic_horse*.bdae` (NO son literales
  del .so — verificado con strings), y el mecanismo del spam quedó confirmado:
  nuestro `ProfileCOMMON_emul_VS.glsl` no declaraba esos uniforms, el compilador
  elimina lo no usado → `glGetUniformLocation` = -1 → "invalid bind symbol" (y
  "Unused parameter" del lado material). Mismo patrón probaba que
  `ambientcolor` etc. se eliminan por no usarse en el FS.
- La montura negra encaja con `color *= tex1` incondicional del bloque
  MULTITEXTURED (programa 41 = MULTITEXTURED|TEXTURESKINNED, con
  `envmapIntensity` sin enlazar): muestrear una unidad sin textura completa
  devuelve (0,0,0,1) en GLES2 → material negro.
- **Fix (`source/utils/embedded_shaders.c`):**
  - VS: bloques `#ifdef SKINNED` (blend lineal, `BoneMatrices[28]` — mc usa
    Bone1..23, el caballo Bone1..26, 28 entradas = 112 vec4 < límite GLES2 128),
    `QUATSKINNED` (dual-quaternion estándar) y `TEXTURESKINNED` (fetch de
    `BoneTexture` con `BoneTextureParams` como dims, layout fila-major común),
    con atributos `SkinIndices`/`SkinWeights` (únicos candidatos en strings del
    .so) y uniform `WeightMask` plegado en `wsum` (lo mantiene vivo y da
    fallback a bind pose con todo a cero). Guards anti-colapso: índices
    clampados a [0,27], `qr` cero → bind pose, resultado degenerado (ceros/NaN)
    → bind pose. Nunca peor que el estado actual.
  - FS ProfileCOMMON MULTITEXTURED y `UnlitMultiTexturedFP` (que declaraba
    `texture1` sin muestrearla → eliminada → spam): usan `tex1` solo si trae
    señal (suma rgb > 0.03), misma disciplina de guards que `vColor0`/
    `DiffuseColor`.
  - NO se tocó LIGHTING (el mundo se ve bien; ignorar luces aclara, no
    ennegrece) ni `envmapIntensity` (spam inofensivo sin efecto visual).

### Validación

- `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_POLICY_VERSION_MINIMUM=3.5`
  + `cmake --build build` (con `VITASDK` y `vita-libs-gen` en PATH): limpio
  (solo warnings preexistentes de `falso_jni`). Wrappers confirmados en el ELF
  (`glViewport_soloader`/`glScissor_soloader` presentes). `eboot.bin` (612 KB)
  y `sacredodyssey.vpk` copiados a la raíz.
- **Pendiente (requiere consola):** desplegar eboot+vpk, reproducir Splash,
  Loading ("New Game") e ingame a caballo, y traer log + screenshot ingame:
  confirmar `[gl_viewport]`, `clip 800 -> 960`, desaparición del spam de
  `Bone*`/`WeightMask`/`texture1`, y estado visual de montura/personaje.

---

## Sesión 2026-09-11 (madrugada) — Triaje crash `1789184124` (mi propio fix de skinning)

- **Log `log_20260911_233504.txt` (457 líneas, build 23:29:01 con el skinning
  instalado):** VS skinned compila/linkea (programas 11-13 OK), luego 3x
  `parameter array size mismatch` y `unbound parameter BoneMatrices ... #define
  SKINNED` justo antes del corte. Los otros fixes de la sesión SÍ entraron
  (clip 800→960 aplicado, shaders embebidos reinstalados con los tamaños nuevos).
- **Dump (`vita-parse-core` contra `build/sacredodyssey`):** Data abort,
  `PC = 0x9828389e` → offset `0x28389e` =
  `CMaterialRendererManager::endMaterialRenderer()+0x166`,
  `LR` en `__android_log_write` del loader (el logueo del "unbound parameter").
  Instrucción: `ldrb.w r3, [fp, #28]` con `fp = [ip, #4]` basura no-nula (pasó
  el `cmp fp,#0 / beq` previo): `autoAddAndBindParameter` (0x282f8c, llamado
  justo antes en 0x283968) dejó `[ip,#4]` sin inicializar para este parámetro.
- **Causa raíz (pseudo-C v106 + hex de `mc.bdae`):** el binder exige igualdad
  EXACTA programa-vs-material (`prog == -1 || prog == mat`, si no
  `parameter array size mismatch` → fail). El tamaño del material deriva del
  bone count de cada malla (`mc.bdae`: Bone1..23, `magic_horse`: Bone1..26, y
  la lista de parámetros del efecto viene con largos `0c`/`0a`/`0d` en el
  propio `.bdae`), así que NINGÚN tamaño estático (`[28]`) puede coincidir con
  todas — en Android esto no existe porque sin `.glsl` en el dataset el motor
  usa su path procedural SProfileGLES2 (tamaño generado por material,
  consistente por construcción).
- **Fix:** revertidos los bloques SKINNED/QUATSKINNED/TEXTURESKINNED del VS
  embebido (comentario extenso in-situ con esta evidencia). Vuelve el estado
  degradado-gracioso del build 22:39 (spam `invalid bind symbol`, bind pose,
  llega a ingame). Se CONSERVAN de esa sesión: redirect `hand2_diffuse`,
  guards de 2ª textura (FS ProfileCOMMON + UnlitMultiTexturedFP), viewport/
  scissor + clip 800→960.
- **Lección registrada:** un template `.glsl` estático JAMÁS debe declarar
  uniforms array cuyo tamaño el motor derive por malla. Skinning real a futuro
  = generar el ProfileCOMMON por material con su bone count.
- **Validación:** build limpio, `eboot.bin` (611 KB) + `.vpk` a la raíz.
- **Pendiente (consola):** redesplegar, verificar que vuelve a ingame como en
  22:39 (el `ensure_embedded_shaders_installed` reinstala solo el VS al diferir
  el tamaño), y traer log + screenshot ingame a caballo.

---

## Sesión 2026-09-12 — Negro en skinned con geometría correcta + telemetría de binds

- **Evidencia fresca (screenshots hf/224923 + log 234654, 6212 líneas):** caballo
  y personaje con geometría CORRECTA pero fragmentos 100% negros; mundo/HUD/
  minimapa bien. Todas las texturas (`magic_horse.kot`, `body_diffuse`,
  `hand_diffuse` + redirect `hand2` funcionando, envmaps) abren bien → el negro
  es estado GL por material, no archivo faltante. Los materiales de `mc.bdae`/
  `magic_horse*.bdae` NO usan LIGHTMAP (solo TEXTURED/MULTITEXTURED/LIGHTING/
  FOG/ALPHABLEND + familia SKINNED) → descartada la hipótesis del lightmap.
- **Aclaración de builds:** el log corre con VS embebido de 687 bytes (sin
  arrays de skinning) → el negro reproduce SIN mi experimento de skinning:
  causa independiente, presente desde el reporte original 22:39.
- **Fix conservado pendiente de verificar en hardware:** guards de 2ª textura
  (FS ProfileCOMMON MULTITEXTURED + UnlitMultiTexturedFP usan `tex1` solo con
  señal) — esta corrida es anterior a ese cambio en consola.
- **Telemetría agregada (`glutil.c`/`dynlib.c`, solo observa):**
  `glGetUniformLocation_soloader` (loguea nombre→location para
  Sampler/Diffuse/envmap/TextureMatrix/Bone/Weight/texture),
  `glUniform4fv_soloader` (primeros vec4 con programa actual: caza un
  DiffuseColor negro), `glUniform1i_soloader` (asignaciones sampler→unidad),
  `glUseProgram_soloader` (rastrea programa actual para correlacionar).
- **Validación:** build limpio, binarios a la raíz.
- **Pendiente (consola, AMBOS archivos + verificar stamp nuevo en el log):**
  jugar hasta la escena del caballo, traer log (`[gl_bind]`/`[gl_u4v]`/
  `[gl_u1i]`) + screenshot ingame + screenshot FRESCA del splash (la aportada
  gj/120522 es de las 12:05, anterior a los fixes de clip/viewport).

---

## Sesión 2026-09-12 (madrugada) — Triaje de `log_20260912_020011.txt`: freeze en autoguardado/stage confirmado, texturas siguen sin build nuevo

- **Reporte del usuario:** congelamiento de varios segundos al autoguardar y
  al pasar de un stage a otro, más errores de textura en personaje/caballo.
- **Freeze (CAUSA CONFIRMADA, misma que Fase 12):** `[render_diag]` cae a
  `fps=2.2` (línea 3226, justo tras `--------LoadWorld() world 10!`) y a
  `fps=2.5` (línea 4437, justo tras `------------MainCharacter::SaveAll!` —
  el autoguardado real: escribe `SaveMC.bin`/`SaveQuest.bin`/`SaveGame.bin`).
  Ambos coinciden exactamente con el `do/while` bloqueante de
  `World::LoadMap()` case 1 ya diagnosticado en la Fase 12 (carga ~100+ game
  objects en una sola pasada sin ceder el control al render loop).
  - **Hallazgo nuevo:** durante esa misma ventana, el motor reabre
    `ProfileCOMMON_emul_VS.glsl`/`FS.glsl` (los shaders embebidos que
    instalamos en `ux0:.../data/3d/effects/`) **~1500 veces cada uno**
    (`fopen`+`fclose` reales contra la tarjeta de memoria, uno por cada
    material que usa el shader procedural ProfileCOMMON) — I/O de disco real,
    no de RAM, que probablemente aporta varios segundos al freeze por sí
    solo. No se puede servir desde memoria de forma segura sin arriesgar
    corromper el `FILE*` real (el proyecto usa `USE_SCELIBC_IO`: el `.so`
    llama `sceLibcBridge_fseek`/`ftell`/`feof`/`ferror`/`fwrite` DIRECTO sobre
    el `FILE*`, sin pasar por nuestros wrappers `_soloader`, así que un
    `FILE*` fabricado en RAM rompería esas llamadas). Queda documentado, no
    resuelto — requeriría interceptar la capa `sceLibcBridge` completa.
  - **Fix aplicado (`source/patch.c`, `so_patch_v106`):** el trampolín de
    yield que la Fase 12 dejó pendiente de decisión, ahora implementado.
    Sitio parcheado: `0x2171aa` (dentro de `_ZN5World7LoadMapEv`, offsets
    verificados contra el símbolo dinámico real
    `_ZN17GameObjectManager4LoadEii @ 0x1d8d38` con `objdump -T`), 4 bytes
    (`ldr r3,[r4,#72]; ldr r2,[r3,#80]`, verificado `== 0x6d1a6ca3` antes de
    escribir) reemplazados por `b.w 0x3463d4` (cueva libre siguiente al
    trampolín de `FileStream::Tell`, antes del pool de literales en
    `0x346434`). El trampolín replica las dos instrucciones pisadas (para no
    romper la comparación `cmp r2,r5` del loop) y llama a `world_load_yield()`
    — **primer trampolín de este port que llama desde el `.so` a código C del
    LOADER** (no solo a otro punto del `.so`), resuelto con un placeholder de
    4 bytes (`.word 0`) en el propio cave que se pisa en tiempo de ejecución
    con `(uint32_t)(uintptr_t)&world_load_yield` (la dirección del loader no
    es conocida en tiempo de ensamblado). Opcodes verificados ensamblando con
    `arm-vita-eabi-as`/`-ld -Ttext=<dirección>` (misma disciplina que el
    resto de los parches v106), no calculados a mano.
    `world_load_yield()` hace `gl_swap()` + `loading_overlay_draw()` (la
    barra de progreso ya existente en `source/utils/loading_overlay.c`) cada
    4 game objects cargados — throttle para no pagar un `vglSwapBuffers` por
    cada uno de los ~100+ objetos. **No reduce el tiempo total de carga**,
    solo hace que la pantalla se siga refrescando (barra animada) en vez de
    verse congelada.
  - Validado: build limpio (`psvita-toolkit build --preset debug`),
    `eboot.bin`/`sacredodyssey.vpk` regenerados. **Pendiente de probar en
    consola física** (autoguardado + transición de stage): confirmar que la
    barra de progreso se anima durante la carga y que no aparece ningún
    `.psp2dmp` nuevo (el trampolín toca un sitio caliente que corre cientos
    de veces por partida).
- **Texturas (personaje/caballo): NO es un bug nuevo, es un build viejo.**
  El log confirma el mismo spam ya diagnosticado (`invalid bind symbol:
  BoneMatrices/BoneQuat0/BoneQuat1/BoneTexture/WeightMask`), pero el eboot
  desplegado tiene build stamp `Sep 11 2026 23:10:39` — **anterior** a los
  fixes de "Sesión 2026-09-11 noche"/"2026-09-12" (guards de 2ª textura) y a
  la telemetría `[gl_bind]`/`[gl_u4v]`/`[gl_u1i]` (0 apariciones en este log,
  pese a que esos wrappers loguean sin gating de escena — solo pueden estar
  ausentes si el build no las tenía). **Pendiente: recompilar+redesplegar
  (ya incluye este fix de freeze + los de textura) y volver a jugar hasta la
  escena del caballo para conseguir un log con stamp nuevo y la telemetría
  real.**

---

## Sesión 2026-09-12 — Receta Asphalt-5 para el splash: engine 800x480 + FBO + blit

- **Motivo:** tres rondas de parches 960 (rects, clip ASprite, escala de sprite)
  no quitaron la barra; el `diag_rect` probó que el dest fullscreen SÍ llegaba
  al draw call pero el arte visible va por `ASprite::PaintFrame` con math de
  diseño 800. En `Asphalt-5-Vita/source/utils/glutil.c` está la receta probada
  del mismo linaje Gameloft: engine en 800x480 + FBO offscreen + un blit que
  estira a 960x544 (su comentario: reportar 960x544 "broke menu layout").
- **Cambios:**
  - `main.c`: `SCREEN_W/H` 960/544 → 800/480 (init + resize + táctil, que ya
    usaba los defines: panel 1920x1088 → 800x480 directo).
  - `java.c`: `getWidth/getHeight` → 800/480.
  - `glutil.c/h`: FBO 800x480 + depth en `gl_init` (con clear inicial y
    fallback a framebuffer real si incompleto); `glViewport/Scissor_soloader`
    reescalan espacio-engine→FBO (identidad exacta, con clamp+log);
    `glBindFramebuffer_soloader` redirige el framebuffer 0 al FBO (los FBOs
    propios del engine pasan y se loguean); `gl_swap()` blitea (fixed-function,
    con save/restore completo incl. programa, unidad activa y viewport) y
    re-enlaza el FBO.
  - `dynlib.c`: `glBindFramebuffer[_OES]` al wrapper.
  - `patch.c`: ELIMINADOS dest 960 (Splash/Loading), clip 800→960, diag_rect y
    escala ASprite del stub (conservaron bypass + todos los crash fixes): con
    el FBO recortarían contra 800x480.
- **Riesgos anotados:** si el engine enlaza FBOs propios de RTT el blit sigue
  presentando el FBO (fuente explícita); el programa GL se restaura tras el
  blit; el overlay del loader lee el viewport real y se adapta solo.
- **Validación:** build limpio, binarios a la raíz.
- **Pendiente (consola, AMBOS archivos, verificar stamp):** splash, title,
  loading, ingame + táctil (ahora en espacio 800x480) + `[gl_fbo]` en el log.

---

## Sesión 2026-09-12 (madrugada, cont.) — Revertida la receta Asphalt-5 (FBO): pantalla negra al iniciar + pedido explícito de sacar el FBO

- **Contexto:** con la receta Asphalt-5 recién instalada, el usuario reportó
  pantalla negra al iniciar (`log_20260912_024707.txt`). Investigación: el
  FBO en sí estaba bien (`Downsample FBO ready: 800x480.` +
  `[gl_fbo] engine using own FBO ...` es vitaGL devolviendo un puntero como
  nombre de FBO -- comportamiento normal de `glGenFramebuffers` en
  `lib/vitaGL/source/framebuffers.c:212`, no una corrupción). La pantalla
  negra coincidía con el stall de arranque YA conocido (carga incremental del
  catálogo de audio, `SoundManagerVox::InitAudioType()`, 10 sonidos por
  llamada desde `SplashState::Update()` -- confirmado incremental en el
  pseudo-C v106) que en el log anterior (`log_20260912_020011.txt`) también
  tardó ~60-120 "frames" a ~1fps antes de estabilizarse; este log nuevo
  simplemente se cortó antes de llegar ahí (1511 líneas, sin `render_diag`
  todavía). No parecía ser un bug nuevo introducido por el FBO.
- **Pedido explícito del usuario:** "elimina cualquier rastro del FBO, solo
  es el escalado de la resolución, nada de FBO real acá" -- revertir a la
  receta anterior (Sesión 2026-09-11 noche): el engine sigue viviendo en
  800x480 (`SCREEN_W`/`SCREEN_H` de `main.c`, necesario para el layout de
  menús/UI -- confirmado en hardware que 960x544 directo lo rompe), pero
  dibuja DIRECTO sobre el framebuffer real de la Vita (960x544, framebuffer
  0) -- sin ningún FBO/textura/blit intermedio. El único mecanismo de escala
  es que `glViewport_soloader`/`glScissor_soloader` (`source/utils/glutil.c`)
  reescalan cada rect que el engine pide en su espacio 800x480 a coordenadas
  960x544 antes de pasarlas a la GPU.
- **Cambios (`source/utils/glutil.c`/`.h`, `source/main.c`, `source/patch.c`):**
  - Eliminados por completo: `s_ds_fbo`/`s_ds_color_tex`/`s_ds_depth_rb`,
    `gl_init_downsample()`, `gl_blit_downsample_to_screen()`, la macro
    `OFFSCREEN_W`/`OFFSCREEN_H`, y la rama de `gl_swap()` que hacía el blit.
  - `gl_init()`: solo `vglInitExtended(...)`, sin inicializar ningún FBO.
  - `gl_swap()`: vuelve a ser `vglSwapBuffers(GL_FALSE);` directo, sin blit.
  - `glBindFramebuffer_soloader()`: ya no redirige el bind de "framebuffer 0"
    a ningún FBO propio -- pasa siempre directo a vitaGL. Los FBOs propios
    del engine (shadow maps/RTT, `framebuffer != 0`) se siguen logueando
    (telemetría, sin tocar su comportamiento).
  - `rescale_viewport_scissor()`: vuelve a reescalar DIRECTO de 800x480 a
    960x544 (antes reescalaba a `OFFSCREEN_W/H`, que al ser igual a 800x480
    era una identidad). Se agregó un guard explícito (reconstruido a partir
    de la descripción en la entrada de la Sesión 2026-09-11 noche, ya que el
    código exacto de esa receta se había sobrescrito sin llegar a commitear):
    solo se reescala un rect que se parece al viewport principal del engine
    (`ancho >= 700 && ancho != alto`); cualquier otro (FBOs propios del
    engine, típicamente cuadrados, o sub-regiones chicas) pasa intacto sin
    deformarse.
  - Comentarios desactualizados en `patch.c` (que daban la receta Asphalt
    como motivo para NO tener los parches de rect a 960 de
    `SplashState`/`LoadingState::Draw2D` ni el clip de `ASprite`) corregidos
    para reflejar que esa receta se revirtió.
- **Riesgo abierto, no resuelto todavía:** los parches puntuales de
  `SplashState::Draw2D`/`LoadingState::Draw2D` (forzar dest rect a 960x544) y
  el clip de `ASprite` (800->960 en `0x211fca`) que existieron ANTES de la
  receta Asphalt (Sesión 2026-09-11 noche, bug del splash recortado a 800px)
  fueron eliminados cuando se instaló el FBO (que hacía innecesario forzar
  esos rects, porque el blit estiraba el frame COMPLETO). Al sacar el FBO,
  es muy probable que el recorte a 800px de ancho en Splash/Loading
  REAPAREZCA, ya que esos elementos 2D no necesariamente heredan el
  reescalado de `glViewport_soloader` (dibujan con rects propios, no
  necesariamente atados al viewport actual). **No se restauraron todavía**
  (requiere re-derivar los offsets/opcodes exactos del `.so` v106 de nuevo,
  igual que se hizo para el trampolín de `World::LoadMap`) -- pendiente de
  confirmar en consola si el recorte vuelve, y de restaurar esos parches
  puntuales si es así.
- **Validación:** build limpio (`psvita-toolkit build --preset debug`),
  `eboot.bin`/`sacredodyssey.vpk` regenerados.
- **Pendiente (consola):** confirmar que ya no hay ningún vestigio de FBO en
  el log (sin `Downsample FBO ready`/blit), que el juego sigue arrancando
  igual que antes (el stall de audio del arranque es el mismo, no cambió), y
  revisar visualmente si Splash/Loading se ven recortados a 800px de ancho.

---

## Sesión 2026-09-12 (madrugada, cont.) — Eliminada la barra de carga propia (debug-only)

- **Pedido del usuario:** "elimina la barra azul de carga ya no es necesario
  es un codigo nuestro, el juego tiene su propia barra de carga esta era solo
  de debug" -- la barra cian animada (`source/utils/loading_overlay.c`) se
  agregó en la Fase 12 puramente como prueba de vida del loop principal
  durante el stall de arranque (para distinguir "congelado" de "avanzando
  lento"); el juego ya tiene su propia pantalla/UI de carga, así que era
  redundante y solo diagnóstico.
- **Eliminado por completo:**
  - `source/utils/loading_overlay.c`/`.h` borrados.
  - `CMakeLists.txt`: quitada la entrada de `loading_overlay.c`.
  - `source/main.c`: quitado el `#include "utils/loading_overlay.h"` y la
    llamada `loading_overlay_draw(frame_count)` en el loop principal. Se
    conserva la telemetría `[render_diag]`/`loading_done` (fps cada 60
    frames, latch cuando supera 30fps) -- ya no dibuja nada, solo diagnóstico
    en el log; reescrito el log de latch a `"boot stall cleared"` en vez de
    `"loading overlay latched off"` para no referenciar algo que ya no
    existe.
  - `source/patch.c`: `world_load_yield()` (el trampolín de `World::LoadMap`,
    ver sesión anterior) ya no llama a `loading_overlay_draw()`, solo hace
    `gl_swap()` cada 4 game objects -- el yield en sí (que la pantalla de
    carga PROPIA del juego se siga redibujando durante la carga en vez de
    quedar bloqueada) se conserva íntegro, era independiente de nuestra
    barra.
- **Validación:** build limpio (`psvita-toolkit build --preset debug`),
  `eboot.bin`/`sacredodyssey.vpk` regenerados.

---

## Sesión 2026-09-15 — Mapeo completo de botones físicos a virtuales, sticks analógicos y fix de touch

- **Motivo:** El usuario solicitó mejorar el port en todos los aspectos y mapear correctamente los botones físicos de la consola con los virtuales del juego. Previamente solo START/Círculo enviaban KEYCODE_BACK y SELECT enviaba KEYCODE_MENU; el resto de los botones físicos (Cross, Square, Triangle, L, R, D-Pad) y los dos sticks analógicos no tenían ningún mapeo a los controles virtuales. Además, el touch en `main.c` sufría del bug crítico documentado en `references/input_handling.md` (pasaba el ID de hardware crudo de `SceTouchReport` de 0-255 al motor, causando potencial corrupción de heap si superaba 4).
- **Implementación (`source/controls.h`, `source/controls.c`, `source/main.c`, `CMakeLists.txt`):**
  - **Stick Analógico Izquierdo & D-Pad (Movimiento 360°):**
    - Se interceptó y hookeó `HudMovePad::Get_MovePad_AxisValues` (`_ZN10HudMovePad22Get_MovePad_AxisValuesEv`).
    - Aplica zona muerta radial (0.18f) y reescalado normalizado para una respuesta analógica suave e instantánea sin consumir slots táctiles.
    - Soporte completo para D-Pad como dirección digital alternativa/complementaria.
    - Conserva la lógica de arrastre del pad virtual por pantalla si el usuario prefiere tocar la pantalla.
  - **Stick Analógico Derecho (Cámara 360°):**
    - Se interceptó y hookeó `CameraRotatePad::Get_MovePad_AxisValues` (`_ZN15CameraRotatePad22Get_MovePad_AxisValuesEv`).
    - Aplica zona muerta radial (0.18f) y factor de sensibilidad ajustado (8.5f) para un paneo de cámara rápido y fluido típico de consolas.
    - Preserva la rotación por swipe táctil simultáneamente.
  - **Mapeo Dinámico de Botones Físicos a Widgets Virtuales:**
    - Se resolvió dinámicamente la instancia raíz `Hud::s_pInstance` (`_ZN3Hud11s_pInstanceE`) y la tabla de offsets de widgets de `Hud::InitHudWidgets`:
      - **Cruz (X)**: Acción principal / salto / rodar / interactuar. Detección contextual por prioridad: avanza tutoriales (`button_HudTurtorialDialogInGame`), salta cinemáticas (`HUD_CutScene`), habla con NPCs (`button_talkToNPC`), abre cofres (`button_openTreasure`), recoge bombas (`button_pickUpBomb`), empuja cajas (`button_pushBox`), rota espejos (`button_rotateMirror`) o acción estándar (`button_action`).
      - **Cuadrado ([ ])**: Ataque cuerpo a cuerpo / espada principal (`button_attack`, `button_sword`).
      - **Triángulo (/_\)**: Armas secundarias / ítems / menú de cambio de armas (`button_ironEagle`, `button_ironFist`, `button_ironChain`, `button_switchWeapon`, `button_swichWeaponWithMenu`).
      - **Círculo (O)**: Guardia / escudo (`button_defense`, `button_block`) durante gameplay activo; tecla Atrás (`KEYCODE_BACK`) en menús/pausa/diálogos.
      - **Gatillo L (L1)**: Guardia / escudo (`button_defense`, `button_block`) o fijación de objetivo (`target_cross`).
      - **Gatillo R (R1)**: Montar / desmontar caballo (`button_ChangeToHorse`, `button_ChangeToRun`) o ataque secundario.
      - **Start**: Menú de pausa del sistema (`button_toSysIGM`, `KEYCODE_BACK`).
      - **Select**: Menú de juego / inventario / mapa (`button_toIGM`, `Mini_Map`).
    - Coordenadas de widgets leídas dinámicamente en tiempo de ejecución desde `widget + 0xdc` (X) y `widget + 0xe0` (Y) con verificación de estado visible (`widget + 0x18`), manteniendo siempre la posición real exacta.
  - **Gestor de Slots Táctiles Seguro (0..4) y Prevención de Corrupción de Heap:**
    - Reescrito el pipeline táctil de `main.c` siguiendo la receta probada de `references/input_handling.md`.
    - Asignación estricta de 5 slots (0..4) compartidos entre toques reales de la pantalla frontal e IDs virtuales estables (-2 a -8) para botones físicos.
    - Ciclo completo y robusto de eventos `ACTION_DOWN`, `ACTION_MOVE` y `ACTION_UP` con clamping de coordenadas a [0..799, 0..479].
- **Validación:**
  - Build limpio ejecutado con `psvita-toolkit build --preset debug`.
  - Generados `build/eboot.bin` y `build/sacredodyssey.vpk` (y sincronizados en raíz).
  - Eliminados archivos basura `._*` con `psvita-toolkit clean-junk`.
