# The Minish Cap — Nintendo Switch (libnx homebrew)

A native Switch port of the [Minish Cap PC port](https://github.com/999sian/tmc)
(itself built on the `zeldaret/tmc` decompilation). The GBA hardware is
reimplemented in software (`libs/ViruaPPU` for graphics, `libs/VirtuaAPU` +
agbplay for sound), so this is **not** a GBA emulator — it's the decompiled
game running natively on the Switch CPU, presented through switch-sdl2.

## Deploy to a Switch (Atmosphère CFW)

The ready-to-copy layout is in `dist/`. Copy it to your SD card so you end up with:

```
sdmc:/switch/tmc/tmc_switch.nro
sdmc:/switch/tmc/baserom.gba      <- your own USA ROM (sha1 b4bd50e4131b027c334547b4524e2dbbd4227130)
```

Then launch **tmc_switch** from the Homebrew Menu (hold R on a game, or Album).

- Supported ROM names in `sdmc:/switch/tmc/`: `baserom.gba` (USA), `baserom_eu.gba`
  (EU), `tmc.gba`, `tmc_eu.gba`. This build is compiled for **USA**.
- **The asset cache is BUNDLED INSIDE the `.nro`** (in its embedded romfs),
  pre-baked on PC by the standalone extractor. On first boot `switch_romfs.c`
  copies it from `romfs:/assets` to `sdmc:/switch/tmc/assets` (one-time, a few
  seconds) so the Switch **skips the slow on-device extraction entirely** — you
  only need to supply `baserom.gba`. (On-device extraction is single-threaded
  because devkitA64 `std::thread` is unreliable, so it would otherwise take many
  minutes and look frozen.) Delete `sdmc:/switch/tmc/assets` to re-seed from the
  `.nro` on the next launch.
- Re-baking the cache: build the extractor and run it on the ROM —
  `bash platforms/switch/build_extractor.sh` (uses Docker `gcc` since there's no
  host C++ toolchain), then `--pak --runtime-only`, then set the recorded
  `rom_mtime` to 0 so the Switch accepts it regardless of the ROM's SD mtime.
- Saves, `config.json`, and caches all live in `sdmc:/switch/tmc/` (the app
  `chdir`s there at startup, so the launch cwd doesn't matter).

## Controls (default)

Joy-Con / Pro Controller map to the GBA buttons via SDL2's game-controller layer
(A/B = A/B, L/R = L/R, +/- = Start/Select, D-pad/stick = D-pad). Rebind in
`config.json` if desired.

## Build

Requires devkitPro (devkitA64 + libnx) and `switch-sdl2`, `switch-zlib`,
`switch-libpng` (`dkp-pacman -S switch-sdl2 switch-zlib switch-libpng`).
Header-only deps (`fmt`, `nlohmann/json`) and a read-only `sys/mman.h` shim are
vendored under `compat/`.

From the **devkitPro MSYS2 shell**:

```sh
cd /path/to/tmc-switch
python platforms/switch/gen_sources.py     # refresh sources.mk from xmake.lua
make -C platforms/switch -j8               # -> platforms/switch/tmc_switch.nro
```

(`gen_sources.py` needs a Python that's on PATH; devkitPro's MSYS2 may not have
one — run it from a normal shell first, then `make` from the dkp shell.)

`make GAME_VER=EU` builds the EU variant.

## How the port is structured (Switch-specific bits)

- `compat/SDL3/SDL.h` + `compat/sdl3_to_sdl2.h` — redirect every `#include
  <SDL3/SDL.h>` to real SDL2 and shim the SDL3 API the port uses onto SDL2.
- `compat/sys/mman.h` — minimal read-only mmap (malloc+read) for the pak loader.
- `compat/{fmt,nlohmann}/` — vendored header-only libraries.
- `switch_stubs.c` — link stub for the one ViruaPPU patch symbol we skip.
- `gen_sources.py` / `sources.mk` — mirror the xmake `tmc_pc` source list.
- Source edits (all `#ifdef __SWITCH__`): SDL2 audio callback (`port_audio.c`),
  software renderer + `chdir` (`port_main.c`), gamepad-by-index +
  c{device,button,axis} events (`port_runtime_config.cpp`), keyboard shortcuts
  compiled out (`port_bios.c`), stubbed update-check (`port_update_check.c`),
  no `execinfo.h` (`port_gameplay_stubs.c`), SD ROM paths (`port_rom.c`).

## Emulator note

Real Tegra hardware runs the switch-mesa (nouveau) GLES shaders that switch-sdl2
emits. **Eden/yuzu cannot** — their Maxwell shader recompiler asserts "Invalid
insn" on Mesa shaders and host-crashes. The build boots, finds the ROM, runs the
engine, and submits frames there; it just can't display under those emulators.
Use real hardware, or Ryujinx (different shader recompiler), to see it render.
A future option is replacing the SDL/GL present with a direct libnx framebuffer
blit (no Mesa) — see the project memory notes — which would also run on Eden.
