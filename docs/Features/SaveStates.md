# Save States — Snapshot, Restore, and the Cross‑Room Crash

A from-scratch save-state system for the port: multi-slot in-memory snapshots of
the running game, a gamepad-navigable management window, and a documented path
to disk persistence. **Currently disabled in the menu** while the last crash is
being resolved.

---

## Index

- [Introduction](#introduction)
- [Plan](#plan)
- [Current status](#current-status)
- [Code locations](#code-locations)
- [The crash, debugged](#the-crash-debugged)
- [Possible next actions](#possible-next-actions)
- [TODO](#todo)
- [Limitations & bugs](#limitations--bugs)

---

## Introduction

**Player-facing goal.** Let the player save the exact game state at any moment
and return to it later — including mid-dungeon and mid-fight — from a menu
reachable with a gamepad (the Switch has no F5/F6 keys), with multiple slots,
and eventually persisting across reboots and `.nro` updates.

**Why the original quicksave wasn't enough.** The port already had an F5/F6
quicksave (`port/port_quicksave.c`), but it was a **single in-memory slot**, had
**no UI** on Switch, and captured only a **subset** of game state — enough to
visually "catch up" on the next frame on PC, but not enough to survive a room
transition. This feature evolves that into a real save-state system.

**Why this is hard here.** This is a **decompilation recompiled natively**, not
an emulator. State that lived in GBA EWRAM was lifted into **native 64-bit
globals** (`port/port_linked_stubs.c`), and pointers became **native 64-bit
addresses**. A snapshot is therefore a graph of cross-referencing pointers, not
a flat block of RAM — which is the source of every difficulty below.

> This fork targets the **Nintendo Switch** only. Disk paths, language
> detection, and the build steps below assume the Switch (libnx) target.

---

## Plan

The work is staged. Stage 1 (same-session) must be rock-solid before Stage 2
(persistence) is attempted.

| Stage | Goal | Pointer handling | Survives reboot? |
|-------|------|------------------|------------------|
| **1 — Same session** | Save/load while the game stays open. Fixes the crash; gives a real freeze-frame save. | None needed — static (BSS) globals keep the same addresses, so restored pointers stay valid. | No |
| **2 — Disk persistence** | Save states survive closing the game and `.nro` updates. | Required — native pointers must be serialized as `(block, offset)` and rebased on load (ASLR moves the base). | Yes |

Disk files (Stage 2) will live in `sdmc:/switch/tmc/` (e.g. `state0.sav` …
`state7.sav`) via relative `fopen`, exactly like `tmc.sav` and `config.json`, so
they survive `.nro` updates (only the executable changes; the SD folder stays).

---

## Current status

- **Stage 1 — in-memory multi-slot + window:** implemented, **but disabled in
  the menu** (see `BuildDisplaySettingsPage` / `BuildMainPage` in
  `port/port_debug_menu.cpp` — the "Save states" entries are commented out)
  until the cross-room crash is confirmed fixed on hardware.
- **Stage 2 — disk persistence:** not started. Directory decision recorded;
  pointer-rebase design outlined below.

The snapshot now captures **~34 regions** (was 11), including the native state
the entity graph references by pointer: `gzHeap`, `gEntityLists` (+ backup),
`gAuxPlayerEntities`, `gPlayerClones`, `gCollidableList`, the manager pool,
`gCarriedEntity`, `gArea`, `gMapBottom/Top`, `gRand`, `gUpdateContext`, and the
script-context side table `gEntityScriptCtxTable`.

---

## Code locations

| Feature | Location | Description |
|---------|----------|-------------|
| Snapshot/restore core | `Port_QuickSave_Slot` / `Port_QuickLoad_DoSlot` in `port/port_quicksave.c` | memcpy each region to/from a per-slot heap buffer. 8 slots. |
| Captured regions | `sRegions[]` in `port/port_quicksave.c` | The list of memory blocks that make up a snapshot. |
| Deferred load | `Port_QuickLoad_RequestSlot` + `Port_QuickSave_TickPendingLoad` in `port/port_quicksave.c`; call site `VBlankIntrWait` in `port/port_bios.c` | Restore runs at a frame boundary, not mid-frame. |
| Save guard | `Port_QuickSave_CanSave` / `Port_QuickSave_StateIsSafe` in `port/port_quicksave.c` | Only save in `GAMETASK_MAIN` / `GAMEMAIN_UPDATE`. |
| Management window | `BuildSaveStatesPage` / `BuildSaveStateSlotPage` in `port/port_debug_menu.cpp` | Slot overview + per-slot Save/Load. **Entries commented out** in `BuildDisplaySettingsPage`/`BuildMainPage`. |
| Pointer-validity guards | `Port_EntityPtrIsValid` / `Port_ListNodeOrHead` in `port/port_entity_ctx.h`; guards in `DeleteAllEntities` / `UnlinkEntity` / `DeleteEntity` in `src/entity.c` | Make entity-list walks immune to a stale node. |
| Script-context NULL guard | `DestroyScriptExecutionContext` in `src/script.c` | Return early on NULL (the last confirmed crash). |
| Manager-pool accessors | `Port_ManagerPoolBase` / `Port_ManagerPoolSize` in `port/port_linked_stubs.c` | Range info for the pointer-validity check. |

---

## The crash, debugged

The save→load→cross-room crash was chased through **four** builds. Each build's
Atmosphère crash report was pulled over the Sphaira FTP server
(`ftp://192.168.1.85:5000/atmosphere/crash_reports/`) and symbolized with
`aarch64-none-elf-addr2line -e tmc_switch.elf <offset>`.

**Exact repro:** save in room A → walk through the door to room B → load the
state (back in A) → walk through the door again → Data Abort.

**Root cause (confirmed by sizeof + register state).** The crash is
`memset(NULL, 0, 0x28)`. `0x28` = **40 bytes** = `sizeof(ScriptExecutionContext)`
(probe-confirmed; 0x24 on GBA, +4 because a `Script*` is 8 bytes on 64-bit).
`addr2line` pointed at `entity.c` only because the `memset` was **inlined** —
the real call is `DestroyScriptExecutionContext` (`src/script.c`) from
`UnloadCutsceneData`, reached when a deleted entity has `ENT_SCRIPTED` set.

The chain:

```
GameTask_Init (src/game.c:142)            ← runs on every room transition
  EraseAllEntities (src/entity.c:260)
    DeleteAllEntities (src/entity.c)       ← walks gEntityLists
      DeleteEntity → UnloadCutsceneData
        DestroyScriptExecutionContext(ctx) ← ctx == NULL
          MemClear(NULL, sizeof(ScriptExecutionContext))  → Data Abort
```

**Why `ctx` is NULL:** the per-entity script context lives in a **side table**,
`gEntityScriptCtxTable` (`port/port_linked_stubs.c`, read via
`Port_GetEntityScriptCtx`). The save state restored `gEntities` (with the
`ENT_SCRIPTED` flag set) **but did not capture that table**; `EraseAllEntities`
zeros it on each transition, so at crash time the slot is NULL. Register `X0=0`
(destination exactly NULL, not garbage) confirmed a zeroed slot rather than a
dangling pointer.

**Fixes applied (A + B):**
- **A** — `DestroyScriptExecutionContext` returns early on NULL (kills the
  crash on any path).
- **B** — the snapshot now captures `gEntityScriptCtxTable` (so scripts actually
  survive a save/load, not just avoid the crash).

Earlier builds also added, and these remain in place as defense-in-depth:
- **Deferred, atomic load** at a frame boundary (was mid-frame).
- **Save guard** to `GAMETASK_MAIN`/`GAMEMAIN_UPDATE`.
- **Pointer-validity guards** in the entity-list delete walk (mirrors the
  existing `i != NULL` guards added for decomp bug #93).
- **Manager pool** removed from the snapshot and zeroed on load (managers are
  per-room transients; restoring stale ones coupled the lists to dead slots).

---

## Possible next actions

Ordered by recommendation.

1. **Verify the A+B fix on hardware (gate for everything else).** Re-run the
   exact repro. If it no longer crashes, Stage 1 is done and the menu entries
   can be re-enabled (uncomment in `port/port_debug_menu.cpp`).
2. **If it still crashes, add the `[BADNODE]` instrumentation.** The port does
   `freopen("/dev/null", stderr)` before `AgbMain`, so a dedicated unbuffered
   `savestate.log` in `sdmc:/switch/tmc/` (same pattern as
   `platforms/switch/switch_applet.c`'s `applet.log`) is needed. Log each entity
   in `DeleteAllEntities` (ptr, kind, id, prev/next/hitbox/myHeap) so the last
   line before the abort names the culprit deterministically — no more guessing
   through inlined `addr2line`.
3. **Then Stage 2 (disk persistence).** Implement pointer serialization:
   walk the captured blocks, convert every native pointer to `(block_id, offset)`
   on save, and rebase to the current base addresses on load. The pointer map is
   already audited (entity `prev/next/parent/child/hitbox/animPtr/myHeap`,
   manager arrays, the circular list sentinel `&gEntityLists[i]`). Write to
   `sdmc:/switch/tmc/state<N>.sav` with a versioned header (magic + offset-table
   build id) so an incompatible file is rejected rather than loaded as garbage.
4. **Alternative if a true freeze-frame proves too fragile:** fall back to
   driving the game's own EEPROM save (the `tmc.sav` path) from the menu —
   100% stable and already persistent, but returns to the room's respawn point
   rather than the exact frame. Lower fidelity, near-zero risk.

---

## TODO

- [ ] Confirm A+B fix on hardware with the exact repro.
- [ ] Re-enable the "Save states" menu entries once verified.
- [ ] Add `savestate.log` instrumentation if any crash remains.
- [ ] Stage 2: pointer serialization + `sdmc:/switch/tmc/state<N>.sav` + versioned header.
- [ ] Decide policy: save states vs. RetroAchievements hardcore mode.

---

## Limitations & bugs

- **Disabled in the menu** right now; the snapshot/restore code is present but
  not reachable from the UI.
- **In-memory only.** Snapshots are lost when the game exits (Stage 2 not done).
- **Same-session pointer assumption.** Restore is only safe within one process
  run; cross-process restore needs the Stage 2 rebase.
- **Region coverage is curated, not exhaustive.** A few small UI/scratch globals
  are intentionally not captured; if a new crash implicates one, add it to
  `sRegions[]`.
- **`addr2line` is unreliable here** due to aggressive inlining — confirm any
  future crash by `sizeof` of the memset/memcpy size and register state, or with
  the `savestate.log` instrumentation, not by the reported line alone.

> Found a save-state crash or a missing region? Please file an issue with the
> Atmosphère crash report and the exact repro steps.
