# Real widescreen (no stretching) — working plan (branch: `widescreen`)

Goal: fill the 16:9 screen with **actual game world** on the sides, no distortion. The frame
is 240x160 (3:2); a true 16:9 at 160 tall is ~284 px wide (160 * 16/9). Target
`MODE1_GBA_WIDTH ≈ 288` (36 tiles) as a round, tile-aligned width.

Branched off `switch-port` @ d5b2650a (fullscreen + overlay + FPS counter, committed).

## ⚠️ Dependency: this branch is GPU... no — CPU-bound by the PPU
The game is already CPU-bound at ~43 fps (single-threaded PPU, see PERF_DOSSIER.md). Widescreen
renders **more columns per scanline → more CPU → fps drops further**. So during development on
this branch expect <43 fps; the **`virtuappu-fps` branch (PPU multithreading) must land before
or with the widescreen merge** for it to be playable. Develop here, but don't merge widescreen
ahead of the threading.

## Two halves

### Half A — render side (ViruaPPU) — medium effort, low risk
The Switch build compiles **unpatched** ViruaPPU + stubs (see PERF_DOSSIER.md), so the
widescreen scaffolding from `port/patches/viruappu-widescreen.patch` is NOT present here. Bring
it into the Switch build:
1. `libs/ViruaPPU/include/cpu/mode1.h`: make `MODE1_GBA_WIDTH` an overridable `#define`
   (default 240) and add `MODE1_GBA_VIEWPORT_X` / `MODE1_GBA_BG_CLIP_X` (the OAM + BG clip
   extents). Set the width via `-DMODE1_GBA_WIDTH=288` in `platforms/switch/Makefile`.
2. `libs/ViruaPPU/src/mode1.c`: clip OAM at `MODE1_GBA_VIEWPORT_X`, clip BG composite at
   `MODE1_GBA_BG_CLIP_X` (force-black past it), per the patch. **Gameplay task only** raises the
   clip to the wide width; static screens (title/file-select/transition) stay clipped at 240
   (their BG buffer is stale past col 240) — reuse port_ppu.cpp's per-task gating.
3. `port/port_ppu.cpp`: the wide framebuffer flows through ComputeFitRect unchanged (it already
   fits any width aspect-correct). **Remove the Phase-1 uniform-stretch block (~line 388)** — we
   want real columns, not a stretch.
4. Capture the ViruaPPU edits as a committed patch (parent ignores submodule dirt) for
   reproducibility.

At this point the frame is 288 wide but cols 240..287 are **black** (engine hasn't filled them).
Honest "no stretch", just not yet wide content. That validates the whole render/present path.

### Half B — engine side (fill the wider BG) — HIGH effort, iterative, HW-tested
This is the real work. The engine's BG tilemap is **256x256 (32x32 tiles, BGCNT screen-size 0**,
e.g. `gScreen.bg0.control = 0x1F0C`); it only keeps ~240px (+ a 16px scroll margin) of valid
columns and updates the leading column as the camera scrolls (`src/scroll.c`, ~31KB; viewport
constants 0xF0=240 / 0xF8=248; tile upload via `src/screenTileMap.c` + `src/vram.c`).
Steps (each verified on real HW by eye, not logs):
1. Switch the gameplay BGs to **512x256 (64x32 tiles, BGCNT screen-size 1)** so >256px of
   columns are valid simultaneously. Touches the BGCNT setup + the VRAM map (tilemap base/size).
2. Make the column-loader fill the **extra columns** on both edges of the camera (find the
   load-leading-column function in scroll.c; widen the visible span from 240 to 288, loading
   ~3 extra tiles each side). Verify scrolling rooms AND static rooms.
3. Reposition / re-center HUD + textboxes that assume 240px (they should stay centered, not
   stretch). Check OAM sprites parked off-screen at x>=240 don't leak into the new visible area.
4. Affine BG2 / mode 7 (PPU mode 2) paths: the title sword, minimaps, some bosses — decide
   per-screen whether to widen or pillarbox.

### Milestones (commit each on this branch)
- W1: Half A in, width=288, building, frame wider with black sides (validates plumbing).
- W2: BGs to 512x256, no regression at 240 content (still 240 visible, just bigger tilemap).
- W3: column-loader fills the extra columns in a simple scrolling room → first REAL widescreen.
- W4: HUD/textbox centering + sprite-leak fixes; static screens stay 240 pillarboxed.
- W5: affine paths + boss/minimap edge cases.

## Open questions to resolve first
- Exact BGCNT/VRAM-map change for 512x256 (which tilemap base, does VRAM have room?).
- The precise scroll.c function that loads the leading column on camera move.
- Does the engine's room/camera clamp assume 240 (camera can't show past room edge)? Wider view
  near room boundaries may reveal out-of-room tiles → may need per-room letterboxing fallback.
