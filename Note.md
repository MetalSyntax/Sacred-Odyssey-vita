El plan de port se ha ejecutado fase a fase conforme a la  
 metodología de SKILL.md y SKILL.md, quedando documentado en detalle
en PORTING_PLAN.md y port_progress.md.  
 ──────

### Resumen de Fases Ejecutadas

#### Fase 0: Prerrequisitos y Toolchain

• Jadx: Localizado y operativo en /opt/homebrew/bin/jadx.  
 • Docker: Verificado con la imagen devrvk/so-decompiler:latest.  
 • VitaSDK: Ubicado en /Users/metalsyntax/vitasdk (GCC 10.3.0).

#### Fase 1: Artefactos de Entrada

• APK extraído: Sacred-Odyssey-The-Raise-of-Ayden-v1.0.3-PowerVR.apk.
• Binario nativo: libsacredodyssey.so (6.8 MB) en ABI armeabi (ARMv6
soft-float, soportado nativamente por la CPU Cortex-A9 de PS Vita).
• Pipeline gráfico: OpenGL ES 2.0 con shaders GLSL.

#### Fase 2: Decompilación Completa

• Java (jadx): Decompilado en decompiled/apk_jadx/ (análisis de  
 clases Game.java, GameRenderer.java, GameGLSurfaceView.java,  
 GLResLoader.java).  
 • Pseudo-C (Ghidra headless en Docker): Ejecutado exitosamente  
 mediante contenedor devrvk/so-decompiler produciendo:  
 • decompiled/decompiled_so/libsacredodyssey_armeabi/out_ghidra.c
(19.1 MB)  
 • decompiled/decompiled_so/libsacredodyssey_armeabi/out_ghidra.h
(2.9 MB)

#### Fase 3: Análisis del Motor Real

• Motor confirmado: Gameloft "Glitch" 3D Engine (el mismo motor  
 utilizado en Shadow Guardian, Dungeon Hunter 2 y Asphalt 5).  
 • Ciclo de vida nativo verificado:  
 1. JNI_OnLoad (inicializa VoxSetJavaVM)  
 2. Device.nativeInit()  
 3. GameRenderer.nativeInit(manuf=0, width=960, height=544)  
 4. GLMediaPlayer.nativeInit()  
 5. MediaPlaylist.nativeInit()  
 6. Game.nativeInit()  
 7. GameRenderer.nativeResize(960, 544)  
 8. Main loop: GameRenderer.nativeRender() y gl_swap().  
 • Importaciones dinámicas del .so: De los 316 símbolos UND, 311  
 fueron resueltos por las librerías del sistema y los 5 restantes  
 requeridos por el motor Glitch fueron implementados en dynlib.c:  
 • \_ZN6glitch4coreL17ROUNDING_ERROR_32E (1.0e-6f)  
 • \_ZN6glitch4coreL17ROUNDING_ERROR_64E (1.0e-8)  
 • **aeabi_d2iz  
 • **dso_handle  
 • inet_addr  
 • Puntos críticos de parcheo:  
 • Redirección de la ruta de datos m_gAppPath mediante hook a  
 \_Z8initPathv() hacia ux0:data/sacredodyssey/.  
 • Bypass de comprobación de licencia  
 ALicenseCheck_ValidateLicense y ValidateNative en patch.c.

#### Fase 4: Repositorio Git y Reglas Anti-DMCA

• .gitignore adaptado para asegurar que ningún archivo propietario  
 (_.apk, _.zip, lib/_.so, /decompiled/, /GloftSOHP/, _.bin, \*.vpk)  
 sea rastreado por git.

#### Fase 5: Bootstrap del Loader y Primer Build Exitoso

• Integración de FalsoJNI en lib/falso_jni/.  
 • Resolución de conflicto de símbolos EGL retirando reimpl/egl.c de
CMakeLists.txt (usando la implementación nativa de vitaGL).  
 • Implementación de game_log() para el logger de FalsoJNI.  
 • Superado el bug de vita-pack-vpk con espacios en la ruta (PSVITA  
 Develop) mediante compilación aislada en /tmp.  
 • Resultado del build: Generación limpia de eboot.bin (601 KB) y  
 sacredodyssey.vpk (683 KB).

#### Fase 6: FalsoJNI y Callbacks Java

• java.c implementado con soporte para:  
 • Carga de recursos (GLResLoader.getResourceLength,
getResourceBytes, getResourceFull).
• Detección de dispositivo Android (SDK_INT = 10,
PlayStationVita, etc.).
• Stubs para ciclo de vida y eventos de sonido (GLMediaPlayer,  
 MediaPlaylist).

#### Fase 7: Main Loop y Muestreo de Entrada

• main.c implementado con:
• Resolución dinámica de todos los métodos nativos del juego.  
 • Overclocking a 444 MHz y asignación de afinidad de CPU a  
 USER_0.
• Secuencia de inicialización exacta.
• Mapeo y escalado táctil frontal a 960x544 (ACTION_DOWN,  
 ACTION_MOVE, ACTION_UP).
• Mapeo de botones físicos (Start / Círculo como KEYCODE_BACK,  
 Select como KEYCODE_MENU).

──────

### Próximo Paso (Fase 8)

1. Colocar la carpeta de datos GloftSOHP/ en ux0:data/sacredodyssey/
   en la consola física o Vita3K.
2. Instalar y ejecutar sacredodyssey.vpk o cargar eboot.bin.
3. Capturar el log de consola para verificar el primer render o  
   triagiar cualquier llamada pendiente.
