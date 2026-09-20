# Sacred Odyssey: Rise of Ayden — PS Vita Port — v0.1.0 (first release)

First playable version of the native port of **Sacred Odyssey: Rise of Ayden** (Android, Gameloft
**Glitch** engine) to PS Vita. Runs the original `libsacredodyssey.so` unmodified through a
SoLoader + FalsoJNI bridge — no game logic was reimplemented, only the Android/JNI/OpenGL ES layer
underneath it.

## What's in this release

- The game boots, loads saved games, and is fully playable from start to finish with a physical
  controller.
- **Full physical control mapping:**
  - Left stick / D-Pad: movement.
  - Right stick: camera rotation (works without needing to touch the screen).
  - Cross, Square, Triangle, Circle, L/R triggers: mapped to the HUD's contextual actions (attack,
    defense, mount/dismount horse, weapon-switch menu, dialogs, treasure chests, bombs, etc.).
  - START: pause / system menu. SELECT: in-game menu / minimap.
  - L+R combo: reveals the full virtual HUD at full opacity (to inspect or directly touch any icon,
    including the ones that stay dimmed during normal gameplay).
- **HUD reorganized for console play:** the icons the original touchscreen UI showed all at once
  (designed for fingers, not a controller) are dimmed to nearly invisible (~1% opacity) during
  normal gameplay, except for the ones that remain relevant information at all times: minimap,
  character portrait/health (`button_toIGM` and `status_healthGroup`), menu icon (`button_toSysIGM`),
  and the weapon-switch icon (`button_switchWeapon`), which remain permanently visible at full opacity.
  The rest of the icons stay touchable (for anyone who prefers playing via the touchscreen), just visually
  in the background.
- **Black-texture fix** on the character and mount (`MULTITEXTURED` materials): the second texture on
  those materials is an additive reflection (envmap) map, not a multiplicative detail layer as an
  earlier pass had assumed — the embedded shader now handles it correctly.
- **Stability fixes:** a crash when mounting the horse (dangling widget pointer), a corrupted file
  cache when invalidating entries during autosave/stage transitions, and several boot-time crashes
  specific to the v1.0.6 build (see `port_progress.md` for the full bug-by-bug detail).
- **Loading improvements:** two real bottlenecks identified and fixed inside `World::LoadMap()` (the
  world game-object loading loop and the scene/graphical-maps loading loop both blocked the entire
  frame until they finished, without yielding control or a single power tick) — stage transitions and
  world-map transitions no longer feel like a total freeze.

## Known issues

- **Erratic FPS:** framerate fluctuates noticeably during normal gameplay (spikes and dips within the
  same scene, short of the severe freezes already fixed during loading transitions). This is the only
  open issue reported as of this release. It will keep being investigated with targeted telemetry in
  upcoming versions.
- See [`port_progress.md`](port_progress.md) for the full history of confirmed bugs and their real
  (not speculative) root cause — this port's policy is one bug at a time, with real-hardware evidence
  before calling it closed.

## Installation

1. Install the generated `.vpk` (`build/sacredodyssey.vpk`) with VitaShell.
2. From the original Android install, copy the following to `ux0:data/sacredodyssey/` on the Vita:
   ```text
   ux0:data/sacredodyssey/
   ├── libsacredodyssey.so
   ├── res/            (drawable, layout, raw)
   └── GloftSOHP/
       └── data/       (3d, 2d, audio, menus, automat, ...)
   ```
3. Requires `kubridge.skprx` and `libshacccg.suprx` installed (see [`README.md`](README.md) for the
   full requirements).
4. Launch the game — the first run creates its own saves/logs under
   `ux0:data/sacredodyssey/saves/` and `ux0:data/sacredodyssey/logs/`.

This repository does **not** distribute the APK, the `.so`, or the game's assets (see `.gitignore`) —
you need your own legitimate copy of Sacred Odyssey: Rise of Ayden.

## Credits

- **Sacred Odyssey: Rise of Ayden** and its engine (Gameloft Glitch) are property of **Gameloft** —
  this repository makes no claim of authorship over the original game.
- **Andy "TheFloW" Nguyen** for the original Android `.so` loader concept.
- **Rinnegatamante** for [vitaGL](https://github.com/Rinnegatamante/vitaGL).
- **Volodymyr Atamanenko** for the soloader boilerplate and FalsoJNI.

## License

This port's code is MIT-licensed (see [`LICENSE`](LICENSE)). The game and its assets remain the
property of Gameloft and are not distributed in this repository.
