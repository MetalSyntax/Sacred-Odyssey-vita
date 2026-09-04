# Sacred Odyssey — Port a PS Vita

Port de `Sacred-Odyssey-The-Raise-of-Ayden-v1.0.3-PowerVR.apk` (Android) a PS Vita vía soloader. Generado con **psvita-port-toolkit**.

## Estructura

- `sacredodyssey_extract/` — APK extraído (gitignored).
- `decompiled/` — Java (jadx) y pseudo-C (Ghidra) del/los .so (gitignored, regenerable).
- `source/`, `lib/so_util`, `lib/falso_jni` — scaffold del boilerplate (SoLoader + FalsoJNI).
- `PORTING_PLAN.md` — plan vivo, actualizar a medida que se confirman cosas del motor real.
- `port_progress.md` — bitácora, un bug confirmado a la vez.
- `.psvita-toolkit.json` — config para el toolkit standalone (build/deploy/logs/LiveArea/crash dumps).

Este port **no** tiene una copia local de `porting_tools/` -- todo el build/deploy/debug se maneja
desde **psvita-port-toolkit**, la herramienta standalone (fuera de este repo). Abrí el toolkit y
elegí "Continuar con un port existente" apuntando a esta carpeta.

## Hallazgos de motor (automáticos, sin confirmar)

- ABI: armeabi (preferida: armeabi)
- GLES: GLES2 (declarado en AndroidManifest.xml)
- Paquete Java: com.gameloft.android.ANMP.GloftSOHP.ML

## Flujo de trabajo esperado

1. Análisis de símbolos antes de tocar loader/source -- skill `psvita-port-init` cubrió la Fase 0-2.
2. Bootstrap del loader guiado por la skill `psvita-porting`.
3. Build/deploy con el toolkit standalone → probar en consola real.
4. Un bug a la vez, guiado por el log real -- skill `so-crash-triage`.
5. Actualizar `port_progress.md` con cada bug confirmado.
