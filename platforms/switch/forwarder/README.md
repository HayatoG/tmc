# Home-screen forwarder (plan)

Goal: an installable NSP that puts a **Minish Cap icon on the Switch home menu**
which launches `sdmc:/switch/tmc/tmc_switch.nro` directly — as an *Application*
(full memory, no "hold R on a game" trick needed).

## Icon

The `.nro` itself carries the icon (256×256 JPG, baked via `elf2nro --icon`),
and forwarder tools reuse it automatically. The repo has no icon yet.

A faithful rip straight from the ROM is impractical: GBA graphics are
LZ-compressed tile banks assembled by a tilemap with per-tile palettes, so the
raw bytes decode as noise (verified). The clean source of real game art is an
**in-game screenshot** (the port runs on hardware): press Capture on the title
screen, then crop/upscale that to 256×256 → `platforms/switch/icon.jpg` →
rebuild the `.nro`.

## Forwarder methods

Both ultimately use **hacBrewPack**, which needs the console's **`prod.keys`**
(dump with Lockpick_RWX). There is no keyless installable NSP.

### A. On-device (no PC keys) — simplest
Run **switch-nsp-forwarder** (TooTallNate) on the Switch: point it at
`sdmc:/switch/tmc/tmc_switch.nro`, set name/author, pick the embedded icon, and
it generates + installs the forwarder right there using the console's keys.

### B. On PC (ready `.nsp`) — needs keys on the PC
Use **NTON** (rlaphoenix) or hacBrewPack with `prod.keys` placed in
`~/.switch/prod.keys` (or `%USERPROFILE%/.switch/`). NTON embeds the NRO and the
icon into a self-contained forwarder NSP:

```
nton build path/to/tmc_switch.nro --name "The Minish Cap" --publisher "..." --icon icon.jpg
```

Produces `The Minish Cap [titleid][v0].nsp` → install with any title installer.

## Status / what's needed
1. Icon: take a title-screen screenshot on the Switch (or supply a 256×256 image).
2. For method B: dump `prod.keys` to the PC.
3. Then: bake icon into the `.nro`, and (B) run NTON to emit the `.nsp`.

Sources: https://github.com/rlaphoenix/nton ,
https://github.com/TooTallNate/switch-nsp-forwarder ,
https://nsp-forwarder.n8.io/
