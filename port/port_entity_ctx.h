/*
 * port_entity_ctx.h — 64-bit safe storage for entity ScriptExecutionContext pointers.
 *
 * On GBA (32-bit), 4-byte pointers are crammed into the entity's cutsceneBeh + field_0x86 fields.
 * On 64-bit PC, 8-byte pointers don't fit there. We use a side table instead.
 */
#ifndef PORT_ENTITY_CTX_H
#define PORT_ENTITY_CTX_H

#include "entity.h"
#include "script.h"
#include <stdint.h>

#ifdef PC_PORT

#define PC_MAX_ENTITY_SLOTS 80 /* 1 player + 7 aux + 72 regular */

extern ScriptExecutionContext* gEntityScriptCtxTable[PC_MAX_ENTITY_SLOTS];

/* Map entity pointer → slot index. Returns -1 if not found. */
static inline int Port_EntitySlot(const Entity* ent) {
    if (ent == &gPlayerEntity.base)
        return 0;
    {
        int i;
        for (i = 0; i < MAX_AUX_PLAYER_ENTITIES; i++)
            if (ent == &gAuxPlayerEntities[i].base)
                return 1 + i;
    }
    {
        int i;
        for (i = 0; i < MAX_ENTITIES; i++)
            if (ent == &gEntities[i].base)
                return 8 + i;
    }
    return -1;
}

/* True if `p` points at a real entity/manager slot — the only valid list
 * nodes. Makes entity-list walks (DeleteAllEntities, UnlinkEntity) immune to a
 * stale pointer left by a save-state restore: a restored list can reference a
 * slot the room reload has since reused, and dereferencing it aborts. Mirrors
 * the #93 NULL-guard pattern already used across this file's list iterations,
 * but range-checks the known containers instead of only checking for NULL.
 * Note: a list sentinel (&gEntityLists[i]) is NOT a slot — callers test for the
 * sentinel separately (they already do via `ent != (Entity*)it`). */
/* Manager-pool base/size via accessors (port_linked_stubs.c) rather than
 * referencing gUnk_02033290 directly: that symbol is declared with conflicting
 * types across the decomp (Manager vs u8 array), so re-declaring it here would
 * clash in translation units that also have the other form. */
unsigned char* Port_ManagerPoolBase(void);
unsigned long  Port_ManagerPoolSize(void);

static inline int Port_EntityPtrIsValid(const void* p) {
    const unsigned char* b = (const unsigned char*)p;
    const unsigned char* pool;
    if (p == NULL || (intptr_t)p < 0) {
        return 0;
    }
    if (b == (const unsigned char*)&gPlayerEntity) {
        return 1;
    }
    if (b >= (const unsigned char*)gAuxPlayerEntities &&
        b < (const unsigned char*)(gAuxPlayerEntities + MAX_AUX_PLAYER_ENTITIES)) {
        return 1;
    }
    if (b >= (const unsigned char*)gEntities &&
        b < (const unsigned char*)(gEntities + MAX_ENTITIES)) {
        return 1;
    }
    pool = Port_ManagerPoolBase();
    if (b >= pool && b < pool + Port_ManagerPoolSize()) {
        return 1;
    }
    return 0;
}

/* Like Port_EntityPtrIsValid but ALSO accepts a list sentinel
 * (&gEntityLists[i]) — used where a prev/next legitimately points at the list
 * head (UnlinkEntity writes through it). gEntityLists is declared in entity.h. */
static inline int Port_ListNodeOrHead(const void* p) {
    const unsigned char* b = (const unsigned char*)p;
    if (Port_EntityPtrIsValid(p)) {
        return 1;
    }
    if (b >= (const unsigned char*)gEntityLists &&
        b < (const unsigned char*)(gEntityLists + 9)) {
        return 1;
    }
    return 0;
}

static inline ScriptExecutionContext* Port_GetEntityScriptCtx(Entity* ent) {
    int slot = Port_EntitySlot(ent);
    return (slot >= 0) ? gEntityScriptCtxTable[slot] : NULL;
}

static inline void Port_SetEntityScriptCtx(Entity* ent, ScriptExecutionContext* ctx) {
    int slot = Port_EntitySlot(ent);
    if (slot >= 0) {
        gEntityScriptCtxTable[slot] = ctx;
        /* Also write to the GenericEntity's scriptContext struct field so that
         * entity-specific structs (BedCover, EzloCap, Stockwell, etc.) which read
         * their own unk_84/context field directly still see the correct value.
         * SKIP for gPlayerEntity (slot 0): PlayerEntity is smaller than GenericEntity
         * and the 8-byte write would overflow past the end of the struct. */
        if (slot > 0) {
            ((GenericEntity*)ent)->scriptContext = ctx;
        }
    }
}

#endif /* PC_PORT */
#endif /* PORT_ENTITY_CTX_H */
