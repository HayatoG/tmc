/*
 * port_quicksave.c — F5 quicksave / F6 quickload.
 *
 * Snapshots a curated set of game-state regions into a heap buffer on
 * F5, then memcpys them back on F6. Keeping this in C so we can name the
 * game globals directly; the C++ debug-menu calls these via the small
 * extern "C" API.
 *
 * Coverage: emulated GBA memory (EWRAM/IWRAM/VRAM/IO), the save file,
 * the player + state, the room controls + transition, gMain, and the
 * full gEntities array. Anything not in this list (HUD state, OAM, gfx
 * slots, palette buffers) will visually catch up over the next frame.
 *
 * Caveats:
 *  - Snapshotting mid-frame is supported but the visible result is
 *    "next frame" — entity logic that ran this frame may have already
 *    written to OAM, which is not snapshotted.
 *  - This does NOT save to disk. The snapshot lives in the process
 *    memory and is lost when the game exits.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "structures.h"
#include "save.h"
#include "main.h"
#include "entity.h"
#include "port_gba_mem.h"

extern u8 gEwram[];
extern u8 gIwram[];
extern u8 gVram[];
extern u8 gIoMem[];

typedef struct {
    void* ptr;
    size_t size;
    const char* name;
} StateRegion;

/* List of regions captured by F5. The order doesn't matter for save,
 * but for restore the order also doesn't matter as long as the regions
 * don't overlap — they don't. */
static StateRegion sRegions[] = {
    { gEwram, 0x40000, "gEwram" },
    { gIwram, 0x8000,  "gIwram" },
    { gVram,  0x18000, "gVram"  },
    { gIoMem, 0x400,   "gIoMem" },
    { &gSave,           sizeof(gSave),           "gSave" },
    { &gPlayerEntity,   sizeof(gPlayerEntity),   "gPlayerEntity" },
    { &gPlayerState,    sizeof(gPlayerState),    "gPlayerState" },
    { &gMain,           sizeof(gMain),           "gMain" },
    { &gRoomControls,   sizeof(gRoomControls),   "gRoomControls" },
    { &gRoomTransition, sizeof(gRoomTransition), "gRoomTransition" },
    { gEntities,        sizeof(gEntities),       "gEntities" },
};

#define NUM_REGIONS (sizeof(sRegions) / sizeof(sRegions[0]))

/* Multi-slot snapshots. Each slot is an independent in-memory snapshot of
 * the regions above. Slot 0 is the legacy F5/F6 quicksave slot, so the old
 * Port_QuickSave()/Port_QuickLoad() entry points still work unchanged.
 *
 * These live in process memory only — they are NOT persisted to disk, so a
 * snapshot is lost when the game exits. (Disk persistence is a separate,
 * riskier change: several captured regions — gEntities, gPlayerEntity,
 * gMain, gRoomControls — hold host pointers that would not survive being
 * reloaded into a fresh process. See port_quicksave.h.) */
#define PORT_QUICKSAVE_SLOTS 8

typedef struct {
    u8*    data;
    size_t bytes;
    int    valid;
} Slot;

static Slot sSlots[PORT_QUICKSAVE_SLOTS];

static size_t TotalRegionBytes(void) {
    size_t total = 0;
    for (size_t i = 0; i < NUM_REGIONS; i++) {
        total += sRegions[i].size;
    }
    return total;
}

int Port_QuickSave_Slot(int slot) {
    if (slot < 0 || slot >= PORT_QUICKSAVE_SLOTS) {
        return 0;
    }
    Slot* s = &sSlots[slot];
    size_t total = TotalRegionBytes();
    if (s->data == NULL || s->bytes != total) {
        free(s->data);
        s->data = (u8*)malloc(total);
        if (s->data == NULL) {
            s->bytes = 0;
            s->valid = 0;
            fprintf(stderr, "[quicksave] slot %d: failed to allocate %zu bytes\n", slot, total);
            return 0;
        }
        s->bytes = total;
    }

    u8* dst = s->data;
    for (size_t i = 0; i < NUM_REGIONS; i++) {
        memcpy(dst, sRegions[i].ptr, sRegions[i].size);
        dst += sRegions[i].size;
    }
    s->valid = 1;
    fprintf(stderr, "[quicksave] slot %d: saved %zu bytes\n", slot, total);
    return 1;
}

int Port_QuickLoad_Slot(int slot) {
    if (slot < 0 || slot >= PORT_QUICKSAVE_SLOTS) {
        return 0;
    }
    Slot* s = &sSlots[slot];
    if (!s->valid || s->data == NULL) {
        return 0;
    }
    const u8* src = s->data;
    for (size_t i = 0; i < NUM_REGIONS; i++) {
        memcpy(sRegions[i].ptr, src, sRegions[i].size);
        src += sRegions[i].size;
    }
    fprintf(stderr, "[quicksave] slot %d: restored %zu bytes\n", slot, s->bytes);
    return 1;
}

int Port_QuickSave_SlotHasSnapshot(int slot) {
    if (slot < 0 || slot >= PORT_QUICKSAVE_SLOTS) {
        return 0;
    }
    return sSlots[slot].valid;
}

int Port_QuickSave_SlotCount(void) {
    return PORT_QUICKSAVE_SLOTS;
}

/* ---- Legacy F5/F6 single-slot API (slot 0) ------------------------------ */

int Port_QuickSave(void) {
    return Port_QuickSave_Slot(0);
}

int Port_QuickLoad(void) {
    return Port_QuickLoad_Slot(0);
}

int Port_QuickSave_HasSnapshot(void) {
    return Port_QuickSave_SlotHasSnapshot(0);
}
