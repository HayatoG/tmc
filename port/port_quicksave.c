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
#include "area.h"
#include "room.h"
#include "script.h"
#include "message.h"
#include "game.h"
#include "port_gba_mem.h"
#include "port_entity_ctx.h"

extern u8 gEwram[];
extern u8 gIwram[];
extern u8 gVram[];
extern u8 gIoMem[];

/* Extra native game-state globals captured by the save state. These live in
 * static (BSS) storage in port_linked_stubs.c — NOT inside the emulated EWRAM
 * blob — so the gEwram snapshot does not cover them. Crucially, several are
 * referenced by pointer from the entity graph (gzHeap via Entity.myHeap /
 * Entity.hitbox; gEntityLists heads via Entity.prev/next; manager pool), so
 * snapshotting gEntities WITHOUT these left dangling pointers after a room
 * transition freed/reused them — that was the save→load→cross-room crash.
 * For same-session save/load these restore to the same static addresses, so
 * the pointers stay valid without any relocation. */
extern u32 gRand;                                  /* RNG state (determinism) */
extern LinkedList gEntityListsBackup[9];
extern Entity* gPlayerClones[3];
extern Entity* gCollidableList[MAX_ENTITIES];
extern ScriptExecutionContext gScriptExecutionContextArray[0x20];
extern FadeControl gFadeControl;
extern Screen gScreen;
extern Message gMessage;
extern PlayerState gPlayerState;
extern MapLayer gMapBottom;
extern MapLayer gMapTop;
extern u8 gUnk_02033290[0x1000];                   /* manager pool (32 Temp) */
extern u8 gzHeap[0x1000];                           /* entity heap allocator */
extern u8 gUpdateContext[64];                       /* entity-update cursor (current_entity) */
/* PC side table: per-entity ScriptExecutionContext pointers (gEntityScriptCtxTable,
 * declared in port_entity_ctx.h). The contexts themselves live in the captured
 * gScriptExecutionContextArray; this table maps entity→context. Not capturing it
 * left ENT_SCRIPTED entities with a NULL context after restore → crash in
 * DestroyScriptExecutionContext on the next room transition. */

typedef struct {
    void* ptr;
    size_t size;
    const char* name;
} StateRegion;

/* List of regions captured by F5. The order doesn't matter for save,
 * but for restore the order also doesn't matter as long as the regions
 * don't overlap — they don't. */
static StateRegion sRegions[] = {
    /* Emulated GBA memory. */
    { gEwram, 0x40000, "gEwram" },
    { gIwram, 0x8000,  "gIwram" },
    { gVram,  0x18000, "gVram"  },
    { gIoMem, 0x400,   "gIoMem" },
    { &gSave,           sizeof(gSave),           "gSave" },

    /* Player + core engine state. */
    { &gRand,           sizeof(gRand),           "gRand" },
    { &gPlayerEntity,   sizeof(gPlayerEntity),   "gPlayerEntity" },
    { &gPlayerState,    sizeof(gPlayerState),    "gPlayerState" },
    { &gMain,           sizeof(gMain),           "gMain" },

    /* Entity system — array, the auxiliary/player pools, the linked-list
     * heads (+ backup), the heap they allocate from, and the manager pool.
     * These reference each other by pointer; capturing them together keeps
     * the graph consistent on restore (same-session: addresses unchanged). */
    { gEntities,           sizeof(gEntities),           "gEntities" },
    { gAuxPlayerEntities,  sizeof(gAuxPlayerEntities),  "gAuxPlayerEntities" },
    { gEntityLists,        sizeof(gEntityLists),        "gEntityLists" },
    { gEntityListsBackup,  sizeof(gEntityListsBackup),  "gEntityListsBackup" },
    { gPlayerClones,       sizeof(gPlayerClones),       "gPlayerClones" },
    { gCollidableList,     sizeof(gCollidableList),     "gCollidableList" },
    { &gCarriedEntity,     sizeof(gCarriedEntity),      "gCarriedEntity" },
    { &gPriorityHandler,   sizeof(gPriorityHandler),    "gPriorityHandler" },
    { &gPossibleInteraction, sizeof(gPossibleInteraction), "gPossibleInteraction" },
    { gzHeap,              sizeof(gzHeap),              "gzHeap" },
    { gUpdateContext,      sizeof(gUpdateContext),      "gUpdateContext" },
    /* NOTE: the manager pool (gUnk_02033290) is deliberately NOT captured.
     * Managers are per-room transient objects; restoring stale ones coupled to
     * gEntityLists is what corrupted the list on the next room transition. The
     * pool is zeroed on load (see Port_QuickLoad_DoSlot) so the room re-creates
     * its managers fresh. */

    /* Room / area / map state. */
    { &gRoomControls,   sizeof(gRoomControls),   "gRoomControls" },
    { &gRoomTransition, sizeof(gRoomTransition), "gRoomTransition" },
    { &gRoomVars,       sizeof(gRoomVars),       "gRoomVars" },
    { &gArea,           sizeof(gArea),           "gArea" },
    { &gMapBottom,      sizeof(gMapBottom),      "gMapBottom" },
    { &gMapTop,         sizeof(gMapTop),         "gMapTop" },

    /* Script + visual state. */
    { &gActiveScriptInfo, sizeof(gActiveScriptInfo), "gActiveScriptInfo" },
    { gScriptExecutionContextArray, sizeof(gScriptExecutionContextArray), "gScriptCtx" },
    { gEntityScriptCtxTable, sizeof(gEntityScriptCtxTable), "gEntityScriptCtxTable" },
    { gActiveItems,     sizeof(gActiveItems),    "gActiveItems" },
    { &gFadeControl,    sizeof(gFadeControl),    "gFadeControl" },
    { &gScreen,         sizeof(gScreen),         "gScreen" },
    { &gMessage,        sizeof(gMessage),        "gMessage" },
    { &gHUD,            sizeof(gHUD),            "gHUD" },
    { &gUI,             sizeof(gUI),             "gUI" },
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
 * reloaded into a fresh process.) */
#define PORT_QUICKSAVE_SLOTS 8

typedef struct {
    u8*    data;
    size_t bytes;
    int    valid;
} Slot;

static Slot sSlots[PORT_QUICKSAVE_SLOTS];

/* Pending deferred load: set by Port_QuickLoad_RequestSlot (called from the
 * menu mid-frame) and consumed by Port_QuickSave_TickPendingLoad at a safe
 * point between frames. Restoring the entity graph mid-frame — while the game
 * is iterating gEntityLists — left dangling next/prev pointers and crashed in
 * DeleteAllEntities; deferring the memcpy to a frame boundary fixes that. */
static int sPendingLoadSlot = -1;

static size_t TotalRegionBytes(void) {
    size_t total = 0;
    for (size_t i = 0; i < NUM_REGIONS; i++) {
        total += sRegions[i].size;
    }
    return total;
}

/* A snapshot is only consistent when taken during stable gameplay — i.e. the
 * game task is GAMETASK_MAIN running its GAMEMAIN_UPDATE substate. Saving in a
 * transition (GAMETASK_INIT, room change) captures a half-built entity graph
 * that crashes when restored. Both save and load gate on this. */
static int Port_QuickSave_StateIsSafe(void) {
    return gMain.state == GAMETASK_MAIN && gMain.substate == GAMEMAIN_UPDATE;
}

int Port_QuickSave_CanSave(void) {
    return Port_QuickSave_StateIsSafe();
}

int Port_QuickSave_Slot(int slot) {
    if (slot < 0 || slot >= PORT_QUICKSAVE_SLOTS) {
        return 0;
    }
    if (!Port_QuickSave_StateIsSafe()) {
        fprintf(stderr, "[quicksave] slot %d: refused — not in stable gameplay\n", slot);
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

/* Immediate restore — only safe to call at a frame boundary (no list walk in
 * progress). Use Port_QuickLoad_RequestSlot from gameplay/menu code instead. */
static int Port_QuickLoad_DoSlot(int slot) {
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
    /* The manager pool is not part of the snapshot (see sRegions note). Zero it
     * so the restored gEntityLists never resolves a sentinel into a stale
     * manager slot; the room re-creates its managers on next update. The
     * DeleteAllEntities/UnlinkEntity guards (src/entity.c) make a lingering
     * gEntityLists reference to a now-zeroed slot harmless. */
    memset(gUnk_02033290, 0, sizeof(gUnk_02033290));
    fprintf(stderr, "[quicksave] slot %d: restored %zu bytes\n", slot, s->bytes);
    return 1;
}

/* Request a load. The actual restore is deferred to Port_QuickSave_TickPendingLoad
 * which runs between frames — restoring the entity graph mid-frame corrupts the
 * list walk and crashes. Returns 1 if the request was accepted (slot valid). */
int Port_QuickLoad_RequestSlot(int slot) {
    if (slot < 0 || slot >= PORT_QUICKSAVE_SLOTS) {
        return 0;
    }
    if (!sSlots[slot].valid || sSlots[slot].data == NULL) {
        return 0;
    }
    sPendingLoadSlot = slot;
    return 1;
}

/* Called once per frame at a safe boundary (top of VBlankIntrWait, before the
 * frame is presented and before game-task dispatch). Performs any pending
 * restore here so the entity graph is swapped atomically between frames. */
void Port_QuickSave_TickPendingLoad(void) {
    if (sPendingLoadSlot < 0) {
        return;
    }
    int slot = sPendingLoadSlot;
    sPendingLoadSlot = -1;
    Port_QuickLoad_DoSlot(slot);
}

/* Back-compat wrapper: the deferred request is the safe path, so route the old
 * synchronous entry point through it. */
int Port_QuickLoad_Slot(int slot) {
    return Port_QuickLoad_RequestSlot(slot);
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
