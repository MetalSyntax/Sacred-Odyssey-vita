# Sacred Odyssey: Rise of Ayden — PS Vita Port

<p align="center">
  <img src="extras/livearea/pic0.png" alt="Sacred Odyssey: Rise of Ayden — PS Vita port banner" width="512">
</p>

<p align="center">
  <img alt="platform" src="https://img.shields.io/badge/platform-PS%20Vita%20%7C%20PSTV-informational">
  <img alt="license" src="https://img.shields.io/badge/license-MIT-blue">
  <img alt="status" src="https://img.shields.io/badge/status-in%20development-yellow">
</p>

Native port of **Sacred Odyssey: Rise of Ayden** (Android, `com.gameloft.android.ANMP.GloftSOHP.ML`,
Gameloft **Glitch** engine) to the PS Vita. Runs the original `libsacredodyssey.so` unmodified through
a [SoLoader](lib/so_util) + [FalsoJNI](lib/falso_jni) bridge — no game logic was reimplemented, only
the Android/JNI/OpenGL ES layer underneath it.

## Status

- 🔧 **In active development.** The game boots, loads saves, and is playable, but the following are
  still being tracked down one confirmed bug at a time (see [`port_progress.md`](port_progress.md)):
  - Physical button + dual-analog-stick mapping to the in-game HUD widgets (movement, camera, attack,
    defense, horse mount/dismount, menus) is implemented and working.
  - A crash while mounted on the horse (dangling `HudWidget*` in the HUD widget table) was root-caused
    from a real console crash dump and fixed.
  - Multi-textured surfaces (partially black character/object textures) were traced to a shader uniform
    naming mismatch (`texture2`) against the engine's own material binder and fixed.
  - FPS drops in scenes with several skinned/animated meshes at once are a known, documented trade-off:
    the hardware-skinning uniform arrays were removed on purpose after they caused a harder crash
    (per-mesh bone-count mismatch), so those meshes render in bind pose with a bind-symbol log warning
    instead of crashing — a real fix requires generating the shader per material's bone count.
- 📋 Full engine findings and porting plan in [`PORTING_PLAN.md`](PORTING_PLAN.md).

## Requirements

- A PS Vita or PSTV on Henkaku/Ensō (or h-encore²/rejuvenate on recent firmware).
- `kubridge` installed (`kubridge.skprx`) — explicitly checked at boot, see `source/utils/init.c`.
- `libshacccg.suprx` installed (required by any homebrew using `vitaGL`/`SceShaccCg` to compile GLSL
  shaders at runtime).
- A legitimate copy of the original Sacred Odyssey: Rise of Ayden Android APK/data. This repository
  does **not** include or distribute the APK, the `.so`, or the game's assets (see `.gitignore`) — only
  the port's loader code.

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
3. Launch the game. The first run creates its own saves/logs under
   `ux0:data/sacredodyssey/saves/` and `ux0:data/sacredodyssey/logs/`.

## Controls

| Vita | In-game action |
|---|---|
| Left stick / D-Pad | Movement (360°, with an added radial deadzone) |
| Right stick | Camera rotation |
| ✕ (Cross) | Contextual action: advance tutorial dialog, skip cutscene, talk to NPC, open treasure, pick up bomb, push box, rotate mirror, or default action |
| □ (Square) | Melee attack / sword |
| △ (Triangle) | Secondary weapons / items / weapon-switch menu |
| ○ (Circle) | Defense / shield in gameplay; Back in menus/dialogs |
| L Trigger | Defense / shield, or target lock |
| R Trigger | Mount / dismount horse, or secondary attack |
| START | Pause / system menu |
| SELECT | In-game menu / inventory / minimap |
| Front touch screen | Full native multitouch, shared with the physical-button mappings above (5 tracked slots) |

Physical buttons are dynamically mapped to whichever on-screen HUD widget is actually active/visible at
that moment (read from the game's own HUD widget table at runtime, see `source/controls.c`), falling
back to a fixed screen position if the corresponding widget isn't currently on screen.

## Building from source

Requires [VitaSDK](https://vitasdk.org/) installed and on your `PATH`.

```sh
export VITASDK=/path/to/vitasdk
export PATH="$VITASDK/bin:$PATH"

mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

It can also be built/deployed with `psvita-port-toolkit` (see below):

```sh
psvita-toolkit build --preset release
psvita-toolkit deploy --vpk
```

## Repository layout

- `source/`, `lib/so_util`, `lib/falso_jni` — the loader (SoLoader + FalsoJNI) and its compatibility
  patches (`patch.c`, `java.c`, `dynlib.c`), plus the physical control mapping (`controls.c`).
- `lib/vitagl` — vendored [vitaGL](https://github.com/Rinnegatamante/vitaGL) submodule.
- `extras/shaders/` — reconstructed GLSL shader sources used by the engine's material system, embedded
  into the eboot at build time (`source/utils/embedded_shaders.c`) so they don't depend on reinstalling
  the full `.vpk`.
- `decompiled/` — decompiled Java (jadx) and pseudo-C (Ghidra) of the original `.so`, used for crash
  triage (gitignored, regenerable).
- `PORTING_PLAN.md` — living porting plan, updated as real engine details get confirmed.
- `port_progress.md` — bug log, one confirmed bug at a time, each with its real root cause and fix.
- `.psvita-toolkit.json` — configuration for `psvita-port-toolkit`, the standalone tool that manages
  this port's build/deploy/logs/LiveArea/crash dumps.

This port does not keep a local copy of `porting_tools/` — all build/deploy/debug work goes through
**psvita-port-toolkit**, outside this repository.

## Development workflow

1. Analyze the `.so`'s symbols before touching the loader (engine: Gameloft Glitch, GLES2 programmable
   pipeline, `.bdae`/Collada-derived models, `ALicenseCheck` anti-tamper).
2. Bootstrap the loader (SoLoader + FalsoJNI + Bionic↔VitaSDK ARM compatibility patches).
3. Build/deploy via `psvita-port-toolkit` → real hardware.
4. One confirmed bug at a time, cross-referencing a real log + `.psp2dmp` + Ghidra pseudo-C +
   decompiled Java source — never a speculative patch without evidence.
5. Every confirmed bug gets documented in `port_progress.md` with its root cause.

## Credits

- **Sacred Odyssey: Rise of Ayden** and its engine (Gameloft Glitch) are property of **Gameloft** — this
  repository makes no claim of authorship over the original game.
- **Andy "TheFloW" Nguyen** for the original Android `.so` loader concept.
- **Rinnegatamante** for [vitaGL](https://github.com/Rinnegatamante/vitaGL) and invaluable soloader
  contributions.
- **Volodymyr Atamanenko** for the soloader boilerplate and FalsoJNI framework.

## License

This port's code is MIT-licensed (see [`LICENSE`](LICENSE)). The game and all of its assets (graphics,
audio, text, `libsacredodyssey.so`) remain the property of **Gameloft** and are **not** distributed in
this repository — you need your own legitimate copy of the game's data to use this port.
