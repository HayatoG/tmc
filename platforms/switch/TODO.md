# Switch port — TODO / roadmap

## Next up — Overlay layout polish (the `-`/Minus quick-settings menu)
The Minus (`-`) button opens the global quick-settings overlay (display
settings + port options, rendered over file-select/name-entry/gameplay). The
layout is functional but rough — improve the visual layout: spacing, alignment,
grouping, readability at handheld vs docked resolutions. Code lives in
`port_debug_menu.cpp` (the overlay renderer) and the Minus-button routing in
`port_bios.c`. Software renderer, so keep redraw regions small (see issue #24
for the broader "overlay from scratch" discussion). **Roadmap: 2026-06-04.**

## Done
- **#28 North Hyrule Field opening fix** — Link/Zelda were spawning off-screen
  (0x8000 scroll-flag UB optimized away by gcc 15 aarch64). Strip the flag with
  a direct bit test. HW-verified. (commit `fbac8756`)
- **Pause-menu NULL-deref crash** in North Hyrule Field (GetOverworldLocation
  → NULL). Guarded. (commit `a888b9b7`)
- **A/B mapped to Nintendo's physical layout** (A→EAST, B→SOUTH). (commit `7456d07e`)
- **[SCENE] room/camera trace** — opt-in debug instrumentation, live over
  `nxlink -s` + persisted to `tmc.log` (append). (commit `1a013c21`)
- Native handheld 720p / docked 1080p output (`de770657`, `switch_applet.c`).
- **Fullscreen on Switch**: window is created `SDL_WINDOW_FULLSCREEN` at the native
  resolution (`port_main.c`), so the present path fills the screen aspect-correct
  instead of rendering into a `240*window_scale` sub-region.
- `Port_PPU_CycleWindowScale` is a no-op on Switch (window scale is meaningless on a
  fixed fullscreen framebuffer; it used to shrink the game into a corner).

## Planned — Input: analog stick moves Link
The left analog stick should move Link (8-way, like the D-pad). Today only the
D-pad maps to the GBA D-pad and the stick does nothing — awkward to play. Map the
SDL gamepad left-stick axes (SDL_GAMEPAD_AXIS_LEFTX / LEFTY) to the GBA DPAD_*
directions in the input poll (port_runtime_config.cpp / port_bios.c), OR-combined
with the D-pad, using a deadzone (~the existing kAxisThreshold). 8-way diagonals.

## Known issue — audio crackle at the widescreen render width on STOCK clocks
At 288px the software PPU + single-threaded game logic + audio mixing are CPU-bound
on a stock Switch (an overclock of CPU/GPU/mem nearly removes the crackle → confirms
CPU/memory-bandwidth starvation, not a logic bug). The game is light and should run
native at stock. Mitigations applied: serial PPU render (POOL_MAX_WORKERS=0, frees
cores 1-2 for the SDL audio thread) + 2048-sample audio buffer. If still tight at
stock: try a narrower widescreen width, pin the audio thread to a dedicated core, or
deeper game-logic/memory optimization. (Threading the render did NOT help fps — game
logic is the bottleneck — so serial render is nearly free.)

## Planned — Real widescreen (16:9 fill, no stretching) — "Phase 2"

### Goal
Fill the 16:9 Switch screen with **actual game world** on the sides instead of
black pillarbox bars, **without** distorting the 240×160 (3:2) image. The current
behavior is correct-but-narrow: aspect-correct fit leaves ~100 px black bars left
and right on 720p. Stretching to fill is rejected (distorts). Widescreen = render
more columns of the scene.

### Why it's not trivial (current state)
- `port/patches/viruappu-widescreen.patch` already makes `MODE1_GBA_WIDTH`
  overridable via `-DMODE1_GBA_WIDTH=N` and adds `MODE1_GBA_VIEWPORT_X` /
  `MODE1_GBA_BG_CLIP_X` (both pinned to 240).
- But the engine's **BG tilemap buffer is only 32 tiles (256 px) wide**, and on
  static screens (title, file-select, cutscene transitions) cols 30–31 hold stale
  scroll-buffer garbage. So BG composition is hard-clipped at col 240
  (`src/mode1.c` `virtuappu_mode1_composite_line`, `MODE1_GBA_BG_CLIP_X`) and OAM at
  240 (`virtuappu_mode1_render_obj_line`, `MODE1_GBA_VIEWPORT_X`).
- **"Phase 1"** (today) = `port/port_ppu.cpp` (~line 388): when
  `MODE1_GBA_WIDTH > 240`, uniform-**stretch** the 240-px frame into the wider
  buffer. This is a placeholder — it visibly stretches, which is exactly what we
  don't want.

### Phase 2 implementation plan
1. **Extend the BG tile buffer to 64 tiles (sa2-style `BGCNT_TXT512x256`).**
   The engine's text BGs need a 512×256 tilemap so columns past 256 hold real,
   camera-correct tile data instead of the 16-px scroll-buffer tail. This is the
   core engine change: widen the BG buffer + the code that fills it per scanline
   from the loaded room tilemap.
2. **Render real BG/OBJ in the extra columns.** Raise `MODE1_GBA_BG_CLIP_X` and
   `MODE1_GBA_VIEWPORT_X` from 240 to the target width (e.g. 360 for a true 9:6→
   ~16.875:9, or 376 for ~16:9 at 160 px tall) **only on the gameplay task** — keep
   static screens (title/file-select/transition) pillarboxed at 240 to avoid the
   stale-VRAM glitch (see the per-task gating already in `port_ppu.cpp`).
3. **Camera / scroll widening.** The engine centers the camera on a 240-px
   viewport. Widening means feeding the renderer extra columns on both edges; verify
   HUD/textbox elements (which assume 240) are repositioned or kept centered.
4. **Drop the Phase-1 stretch** in `port_ppu.cpp` once BG/OBJ render natively wide.
5. **Edge cases to test:** room transitions, scrolling vs. static rooms, affine BG2
   (Mode 2 / title sword), sprites parked off-screen at x≥240 (must not leak into
   the new visible area), minimap/menus, EU vs USA layout.

### Effort estimate
High — this is an **engine-level** change (BG buffer geometry + tile fill +
camera), not just a host/present tweak. Budget several focused sessions. The host
side (`port_ppu.cpp` fit/stretch, `MODE1_GBA_WIDTH` define) is already wired for it.

### Files in play
- `port/patches/viruappu-widescreen.patch` (BG/OAM clip extents, `MODE1_GBA_WIDTH`)
- `src/mode1.c` (`virtuappu_mode1_composite_line`, `virtuappu_mode1_render_obj_line`,
  `virtuappu_mode1_render_text_bg_line`)
- `include/cpu/mode1.h` (`MODE1_GBA_WIDTH`, `MODE1_GBA_BG_CLIP_X`, viewport)
- `port/port_ppu.cpp` (`Port_PPU_PresentFrame` stretch block, `ComputeFitRect`)
- Build define: `-DMODE1_GBA_WIDTH=N` in `platforms/switch/Makefile`
