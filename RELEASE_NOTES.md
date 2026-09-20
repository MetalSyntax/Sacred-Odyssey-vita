# Sacred Odyssey: Rise of Ayden — PS Vita Port — v0.1.0 (primer release)

Primera versión jugable del port nativo de **Sacred Odyssey: Rise of Ayden** (Android, Gameloft
**Glitch** engine) a PS Vita. Corre el `libsacredodyssey.so` original sin modificar, a través de un
puente SoLoader + FalsoJNI — no se reimplementó lógica del juego, solo la capa Android/JNI/OpenGL ES
por debajo.

## Qué incluye este release

- El juego arranca, carga partidas guardadas y es jugable de principio a fin con mando físico.
- **Controles físicos completos:**
  - Stick izquierdo / D-Pad: movimiento.
  - Stick derecho: rotación de cámara (funciona sin necesidad de tocar la pantalla).
  - Cruz, Cuadrado, Triángulo, Círculo, gatillos L/R: mapeados a las acciones contextuales del HUD
    (ataque, defensa, montar/desmontar caballo, menú de armas, diálogos, cofres, bombas, etc.).
  - START: pausa/menú de sistema. SELECT: menú en partida / minimapa.
  - Combo L+R: revela el HUD virtual completo a opacidad total (para inspeccionar o tocar directo
    cualquier ícono, incluso los que quedan atenuados durante el juego normal).
- **HUD reorganizado para consola:** los íconos que la pantalla táctil original mostraba todos a la vez
  (pensados para dedos, no para un mando) se atenúan durante el juego normal, salvo los que siguen
  siendo información relevante todo el tiempo: minimapa, retrato/vida del personaje, ícono de menú y el
  ícono de cambio de arma. El resto de los íconos siguen tocables (para quien prefiera jugar con la
  pantalla), solo más discretos visualmente.
- **Corrección de texturas negras** en personaje y montura (materiales `MULTITEXTURED`): la segunda
  textura de esos materiales es un mapa de reflejo (envmap) aditivo, no un detalle multiplicativo como
  se interpretó en una primera pasada — el shader embebido ya lo trata correctamente.
- **Fixes de estabilidad:** crash al montar el caballo (puntero de widget colgante), fcache de
  archivos corrupto al invalidar entradas durante autoguardado/transición de stage, y varios crashes de
  arranque específicos del build v1.0.6 (ver `port_progress.md` para el detalle completo, bug por bug).
- **Mejoras de carga:** dos cuellos de botella reales identificados y corregidos en `World::LoadMap()`
  (el loop de carga de objetos del mundo y el de mapas gráficos/texturas de escena bloqueaban el frame
  completo hasta terminar, sin ceder control ni un tick de energía) — las transiciones de stage y de
  mapa del mundo ya no se sienten como un congelamiento total.

## Problemas conocidos

- **FPS erráticos:** el framerate fluctúa de forma perceptible durante el juego normal (picos y caídas
  dentro de una misma escena, sin llegar a los congelamientos severos ya corregidos en las transiciones
  de carga). Es el único problema abierto reportado hasta este release. Seguirá siendo investigado con
  telemetría dirigida en próximas versiones.
- Ver [`port_progress.md`](port_progress.md) para el historial completo de bugs confirmados y su causa
  raíz real (no especulada) — la política de este port es un bug a la vez, con evidencia de consola
  física antes de darlo por cerrado.

## Instalación

1. Instalar el `.vpk` generado (`build/sacredodyssey.vpk`) con VitaShell.
2. Desde la instalación Android original, copiar a `ux0:data/sacredodyssey/` en la Vita:
   ```text
   ux0:data/sacredodyssey/
   ├── libsacredodyssey.so
   ├── res/            (drawable, layout, raw)
   └── GloftSOHP/
       └── data/       (3d, 2d, audio, menus, automat, ...)
   ```
3. Requiere `kubridge.skprx` y `libshacccg.suprx` instalados (ver [`README.md`](README.md) para el
   detalle de requisitos).
4. Lanzar el juego — la primera ejecución crea sus propios saves/logs bajo
   `ux0:data/sacredodyssey/saves/` y `ux0:data/sacredodyssey/logs/`.

Este repositorio **no** distribuye el APK, el `.so` ni los assets del juego (ver `.gitignore`) — hace
falta una copia legítima propia de Sacred Odyssey: Rise of Ayden.

## Créditos

- **Sacred Odyssey: Rise of Ayden** y su motor (Gameloft Glitch) son propiedad de **Gameloft** — este
  repositorio no reclama autoría sobre el juego original.
- **Andy "TheFloW" Nguyen** por el concepto original del loader Android `.so`.
- **Rinnegatamante** por [vitaGL](https://github.com/Rinnegatamante/vitaGL).
- **Volodymyr Atamanenko** por el boilerplate del soloader y FalsoJNI.

## Licencia

El código de este port es MIT (ver [`LICENSE`](LICENSE)). El juego y sus assets siguen siendo propiedad
de Gameloft y no se distribuyen en este repositorio.
