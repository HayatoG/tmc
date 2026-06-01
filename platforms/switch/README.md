# The Minish Cap — Nintendo Switch port

**Language:** English · [Português](README.pt-BR.md)

A native Nintendo Switch (libnx homebrew) build of the Minish Cap
[PC port](https://github.com/999sian/tmc) (which is itself built on the
[`zeldaret/tmc`](https://github.com/zeldaret/tmc) decompilation).

The GBA hardware is reimplemented **in software** (`libs/ViruaPPU` for the
picture unit, `libs/VirtuaAPU` + agbplay for sound), so this is **not a GBA
emulator** — it's the decompiled game running natively on the Switch CPU, with
the resulting framebuffer presented through switch-sdl2. Because rendering is
pure CPU → framebuffer, no GPU translation is needed.

> ⚠️ You must supply your own game ROM. None is included.

---

## ▶️ Play it (quick start)

You need a Switch with **Atmosphère** CFW and the Homebrew Menu.

### 1. Copy two files to your SD card

Create the folder `sdmc:/switch/tmc/` and put **both** of these in it:

```
sdmc:/switch/tmc/
  ├── tmc_switch.nro     ← the homebrew (asset cache is bundled inside it)
  └── <your USA ROM>.gba ← your own copy of the game
```

- The ROM must be the **USA** version of The Minish Cap
  (sha1 `b4bd50e4131b027c334547b4524e2dbbd4227130`).
- **The filename does not matter** — drop your `.gba` in with any name. The port
  identifies the right ROM by its contents, so `baserom.gba`, `tmc.gba`, or
  `Zelda Minish Cap.gba` all work. (Older builds required the exact name
  `baserom.gba`; that's no longer the case.)

### 2. Launch it with **full memory**

The port needs **Application-mode memory** — it will **not** run as a library
applet. So you must open the Homebrew Menu the "full memory" way:

- **Hold `R` while launching any installed game** to enter the Homebrew Menu, **or**
- Use an **NSP forwarder** (see [Home-screen forwarder](#-home-screen-forwarder-optional) below).

> Opening homebrew from the **Album** runs it as an applet with limited memory —
> the game will fail to start that way. Use the hold-`R` method or a forwarder.

Then launch **The Minish Cap** from the menu.

**First launch** takes a few extra seconds (one time): it extracts ROM pages and
seeds the bundled asset cache to the SD. Every launch after that is fast. Saves,
config, and caches all live in `sdmc:/switch/tmc/`.

---

## 🎮 Controls

Joy-Con / Pro Controller are mapped to the GBA buttons automatically:

| Switch | GBA |
|---|---|
| A / B | A / B |
| L / R | L / R |
| + / − | Start / Select |
| D-Pad / Left Stick | D-Pad |

You can rebind in `sdmc:/switch/tmc/config.json`.

---

## 🔨 Build from source

**Requirements:** [devkitPro](https://devkitpro.org/) with devkitA64 + libnx,
plus the portlibs `switch-sdl2`, `switch-zlib`, `switch-libpng`:

```sh
dkp-pacman -S switch-dev switch-sdl2 switch-zlib switch-libpng
```

Header-only deps (`fmt`, `nlohmann/json`) and a read-only `sys/mman.h` shim are
vendored under `platforms/switch/compat/`.

From the **devkitPro MSYS2 shell** (so `/opt/devkitpro` is mounted), in the repo root:

```sh
git submodule update --init                  # ViruaPPU + VirtuaAPU
python platforms/switch/gen_sources.py        # refresh sources.mk from xmake.lua
make -C platforms/switch -j8                  # -> platforms/switch/tmc_switch.nro
```

- `gen_sources.py` needs a Python on PATH; devkitPro's MSYS2 may not have one —
  run it from a normal shell first, then `make` from the dkP shell.
- `make GAME_VER=EU` builds the EU variant.
- Without a pre-baked cache in `romfs/`, the `.nro` is smaller and falls back to
  on-device extraction (slow — see below).

---

## 📦 Pre-baking the asset cache (recommended)

On-device asset extraction is **single-threaded** (devkitA64's `std::thread` is
unreliable, so the parallel path is disabled), which makes a cold first run take
many minutes and look frozen. To avoid that, the cache is pre-baked on PC and
**bundled into the `.nro`'s romfs**, so the Switch skips extraction entirely.

```sh
bash platforms/switch/build_extractor.sh   # builds the extractor in a Docker gcc
                                           # image, runs it on baserom.gba, and
                                           # stages the cache into romfs/ + dist/
make -C platforms/switch -j8               # rebuild so elf2nro embeds romfs/
```

The script sets the recorded `rom_mtime` in `.asset_build_state.json` to `0` so
the Switch accepts the cache regardless of the ROM's mtime on the SD (it still
checks ROM size + pack format).

On first boot `switch_romfs.c` copies `romfs:/assets` → `sdmc:/switch/tmc/assets`
if it's missing, so a user only needs the `.nro` + their `baserom.gba`.

---

## 🖼️ Custom icon

The home/hbmenu icon is a 256×256 JPEG baked into the `.nro` (`elf2nro --icon`).
`platforms/switch/icon.jpg` is used if present (else devkitPro's default). To
regenerate from a source image, drop it at `platforms/switch/icon_src.png` and
resize to 256×256, or use `gen_icon.py` for a themed placeholder.

---

## 🏠 Home-screen forwarder (optional)

Want a Minish Cap icon on the Switch **home menu** (instead of going through the
Homebrew Menu)? That needs an installable NSP forwarder. See
[`forwarder/README.md`](forwarder/README.md). Short version:

- **On-device (no PC keys):** the `switch-nsp-forwarder` app — point it at
  `sdmc:/switch/tmc/tmc_switch.nro`; it reuses the embedded icon.
- **On PC:** [NTON](https://github.com/rlaphoenix/nton) or hacBrewPack, which
  need your console's `prod.keys` on the PC.

A forwarder also launches the `.nro` as an *Application* (full memory), so the
"hold R" trick isn't needed.

---

## 🩺 Troubleshooting

- **Boot log:** `sdmc:/switch/tmc/tmc.log` (unbuffered — survives a freeze). Over
  USB/MTP, Windows hides the `.log` extension so it shows as `tmc` of type
  *txtfile* (there are two `tmc` entries — the txtfile is the log, the SAV is
  your save).
- **First boot is slow / "stuck":** the one-time extraction. Let it finish.
- **Wrong region:** the log prints the detected region; use the matching build
  (`GAME_VER`) and ROM.
- **Emulators:** real Tegra hardware runs the switch-mesa (nouveau) GLES shaders
  switch-sdl2 emits. **Eden/yuzu cannot** (their Maxwell shader recompiler
  asserts on Mesa shaders and crashes) — the app boots and runs there but won't
  display. Use real hardware, or Ryujinx (different shader recompiler).

---

## 🧩 How it works (Switch-specific bits)

- `compat/SDL3/SDL.h` + `compat/sdl3_to_sdl2.h` — redirect every
  `#include <SDL3/SDL.h>` to real SDL2 and shim the SDL3 API the port uses
  (devkitPro ships only SDL2).
- `compat/sys/mman.h` — minimal read-only mmap (malloc + read) for the pak loader.
- `compat/{fmt,nlohmann}/` — vendored header-only libraries.
- `switch_stubs.c` — link stub for the one ViruaPPU patch symbol we skip.
- `switch_romfs.c` — mounts the embedded romfs and seeds the SD asset cache.
- `gen_sources.py` / `sources.mk` — mirror the xmake `tmc_pc` source list.
- `#ifdef __SWITCH__` adaptations across `port/`: SDL2 callback audio, software
  renderer, `chdir` to `/switch/tmc`, file logging, gamepad-by-index + SDL2 event
  fields, keyboard shortcuts compiled out, stubbed update-check, no `execinfo`,
  serial `ParallelFor`, SD ROM paths, and the per-frame debug spam silenced
  before the game loop.

---

## Credits & links

- Decompilation: [zeldaret/tmc](https://github.com/zeldaret/tmc)
- PC port: [999sian/tmc](https://github.com/999sian/tmc)
- Software GBA hw: [ViruaPPU/VirtuaAPU](https://github.com/MatheoVignaud),
  [agbplay](https://github.com/ipatix/agbplay)
- Toolchain: [devkitPro](https://devkitpro.org/) ·
  [libnx](https://github.com/switchbrew/libnx) ·
  [switch-sdl2](https://github.com/devkitPro/SDL)
