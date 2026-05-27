# Switch performance dossier — why ~43-44 fps (not 60)

Measured on real hardware via the in-overlay FPS counter (Minus → "FPS counter on"):
**~43-44 fps** in normal gameplay, default settings (internal_scale=1, no xBRZ/CRT).
VSync is off and the frame cap is high, so 43-44 is the **raw CPU throughput**, i.e. the
game is **CPU-bound**, not display- or vsync-limited.

## Prime suspect (high confidence): the software PPU runs single-threaded

- **ViruaPPU renders per scanline on the CPU.** The hot path is
  `virtuappu_mode1_render_frame` (GBA mode 0→PPU mode 1, mode 1/2→PPU mode 2): for each of
  160 lines it composites up to 4 BG layers + 128 OAM sprites.
- On **PC it is parallelized with OpenMP** — `xmake.lua:672-708` deliberately enables
  `-fopenmp` + `-DUSE_OPENMP`, with the comment *"VirtuaPPU is compiled directly into tmc_pc,
  so OpenMP must be enabled here."* The render loops are `#pragma omp parallel for`
  (`libs/ViruaPPU/src/mode0.c:208`, and the widescreen patch's `mode1.c` render_frame).
- The **Switch Makefile passes NEITHER `-fopenmp` NOR `-DUSE_OPENMP`** (`platforms/switch/Makefile`,
  no openmp flag; LIBS has no `-lgomp`). Without `-fopenmp` those `#pragma omp` lines are
  **silently ignored** → all 160 scanlines composite **serially on one Cortex-A57 core**.
- The A57 at ~1.02 GHz doing the full GBA PPU composite serially at 60 Hz is right on the edge;
  ~43 fps fits a single-core bottleneck.

**Fix options (in order of preference):**
1. **Reuse `switch_parallel_for`** (already written, `platforms/switch/switch_parallel.c` —
   libnx work-stealing parallel-for across up to 3 Application-mode cores). Wrap the per-line
   render loop in mode1/mode2 (and mode0) with a `#ifdef __SWITCH__` path that calls
   `switch_parallel_for(160, N, render_one_line, ctx)` instead of the omp pragma. The widescreen
   patch already restructured mode1 into a "snapshot IO per line, then render lines
   independently" shape — that is *exactly* the parallel-safe form this needs. Expected: near-linear
   speedup with cores (2 cores ≈ ~75-85 fps ceiling, capped back to 60). **This is the single
   highest-value change and it is low risk** (the data flow is already proven parallel on PC).
2. Enable real OpenMP on devkitA64 *if* `libgomp` ships for aarch64-none-elf (uncertain; needs
   `-fopenmp -lgomp`). If it links and runs, it's a one-line build change. Verify it doesn't hit
   the same `std::thread`-style breakage libnx has — OpenMP uses pthreads, which libnx supports,
   so it *may* work. Test in isolation before trusting.

## Secondary suspects (lower confidence — confirm by instrumenting)

- **Audio mixing (agbplay/VirtuaAPU MP2K).** Per the port design audio runs in the SDL2
  pull-callback (a separate thread), so it should NOT cost main-thread frame time — but it still
  consumes a core, which matters once we multithread the PPU (don't oversubscribe). Confirm the
  callback thread's core affinity / cost.
- **Frame present (scale-blit + filters).** At default settings this is one textured-quad blit
  (cheap). Only relevant if the user turns on xBRZ (4x CPU upscale) or a CRT filter or
  internal_scale>1 — those are CPU-heavy and would compound the drop. Not the baseline cause.
- **Frame pacing.** `VBlankIntrWait` busy-waits to a deadline. With a 144-fps target + vsync off,
  the busy-wait is short and not the bottleneck; but verify the deadline math isn't capping below
  the achievable rate.
- **CPU clock / governor.** Homebrew can't raise the A57 clock without `sys-clk`. Docked vs
  handheld CPU clock is similar; GPU differs (irrelevant — we're CPU-bound). Note for the user:
  a `sys-clk` overclock would raise the ceiling but is out of scope for the port.

## How to confirm (cheap instrumentation)
Add coarse timers around the three main-thread costs for a few seconds and log to
`sdmc:/switch/tmc/perf.log`: (a) `virtuappu_render_frame`, (b) the present/blit, (c) AgbMain
game-tick. If (a) dominates → PPU threading is the fix (expected). Do this before/after the
threading change to quantify the win.

## ⚠️ Interaction with real widescreen (Phase 2 — see TODO.md)
Real widescreen renders **more columns per scanline** → it **increases** the per-line PPU cost,
so doing widescreen on the current single-threaded PPU would push fps **below 43**. Therefore the
**PPU multithreading (fix #1) should land before or together with widescreen** — it is effectively
a prerequisite for widescreen to stay playable. The widescreen patch's per-line snapshot shape and
the threading fix share the same code, so they're natural to do in one pass.
