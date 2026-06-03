/*
 * port_debug_menu.cpp — F8 in-game debug menu.
 *
 * Renders an SDL overlay using SDL_RenderDebugText. While open, all game
 * input is masked (Port_UpdateInput consults Port_DebugMenu_IsOpen) and
 * SDL key events are routed to the menu instead of the game.
 *
 * Pages are an array of items; each item is either a submenu pointer or
 * a callable action. Up/Down navigates, Enter activates, B/Esc backs out
 * (and closes the menu when at the top level).
 *
 * Game-state mutations live in port_debug_actions.c so this TU doesn't
 * need to include the game headers (which don't parse as C++ — they use
 * `this` as a parameter name).
 */

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "port_debug_menu.h"

extern "C" {
#ifdef __SWITCH__
/* RetroAchievements (issue #12) — implemented in port_retroachievements.c. */
int Port_RA_InteractiveLogin(void);
int Port_RA_IsLoggedIn(void);
const char* Port_RA_UserName(void);
void Port_RA_SimulateUnlock(void); /* debug toast, issue #25 */
#endif
void Port_DebugAction_GiveAllItems(void);
void Port_DebugAction_MaxHearts(void);
void Port_DebugAction_HealFull(void);
void Port_DebugAction_MaxRupees(void);
void Port_DebugAction_MaxShells(void);
void Port_DebugAction_AllKinstones(void);
int Port_DebugAction_Warp(unsigned char area, unsigned char room,
                          unsigned short x, unsigned short y,
                          unsigned char layer);
int Port_DebugQuery_AreaRoomCount(unsigned char area);
int Port_DebugQuery_RoomDimensions(unsigned char area, unsigned char room,
                                   unsigned short* w, unsigned short* h);
const char* Port_DebugQuery_AreaName(unsigned char area);

/* Display / runtime-config knobs — same set the file-select "L Settings"
 * panel exposes, so the F8 menu can drive them mid-game. */
void          Port_PPU_ToggleFullscreen(void);
bool          Port_PPU_IsFullscreen(void);
void          Port_PPU_CycleWindowScale(int direction);
unsigned char Port_PPU_WindowScale(void);
void          Port_PPU_CyclePresentationMode(int direction);
const char*   Port_PPU_PresentationModeName(void);
void          Port_PPU_CycleFilter(int direction);
const char*   Port_PPU_FilterName(void);
unsigned int  Port_Config_TargetFps(void);
void          Port_Config_CycleTargetFps(int direction);
unsigned char Port_Config_InternalScale(void);
void          Port_Config_CycleInternalScale(int direction);
bool          Port_Config_ShowFps(void);
void          Port_Config_ToggleShowFps(void);
int           Port_Config_FpsCorner(void);
void          Port_Config_CycleFpsCorner(int direction);
int           Port_Config_FpsScale(void);
void          Port_Config_CycleFpsScale(int direction);
bool          Port_Config_FpsBackground(void);
void          Port_Config_ToggleFpsBackground(void);
int           Port_Config_Language(void);          /* 0 = EN, 1 = PT */
void          Port_Config_CycleLanguage(int direction);
int           Port_Config_RaOverlayVariant(void);  /* 0..5, see kRaVariantNames */
void          Port_Config_CycleRaOverlayVariant(int direction);

/* Soft-slot equip-button assignments (port_softslots.c). */
const char*   Port_SoftSlots_GetSlotLabel(int slot);
void          Port_SoftSlots_CycleAssignment(int slot, int direction);

/* Multi-slot save states (port_quicksave.c). In-memory only — see that
 * file's header. Drives the "Save states" menu page so save/load is
 * reachable with a gamepad (the Switch has no F5/F6 keys). */
int           Port_QuickSave_Slot(int slot);
int           Port_QuickLoad_Slot(int slot);
int           Port_QuickSave_SlotHasSnapshot(int slot);
int           Port_QuickSave_SlotCount(void);
int           Port_QuickSave_CanSave(void);
}

namespace {

/* Mirror a few enum values from include/area.h here so the menu doesn't
 * pull in the game headers. Update if these area indices ever change. */
constexpr unsigned char AREA_MINISH_WOODS              = 0x00;
constexpr unsigned char AREA_MINISH_VILLAGE            = 0x01;
constexpr unsigned char AREA_HYRULE_TOWN               = 0x02;
constexpr unsigned char AREA_HYRULE_FIELD              = 0x03;
constexpr unsigned char AREA_MT_CRENEL                 = 0x06;
constexpr unsigned char AREA_MELARIS_MINE              = 0x10;
constexpr unsigned char AREA_DEEPWOOD_SHRINE           = 0x48;
constexpr unsigned char AREA_DEEPWOOD_SHRINE_BOSS      = 0x49;
constexpr unsigned char AREA_DEEPWOOD_SHRINE_ENTRY     = 0x4A;
constexpr unsigned char AREA_CAVE_OF_FLAMES            = 0x50;
constexpr unsigned char AREA_CAVE_OF_FLAMES_BOSS       = 0x51;
constexpr unsigned char AREA_FORTRESS_OF_WINDS         = 0x58;
constexpr unsigned char AREA_TEMPLE_OF_DROPLETS        = 0x60;
constexpr unsigned char AREA_ROYAL_CRYPT               = 0x68;
constexpr unsigned char AREA_PALACE_OF_WINDS           = 0x70;

bool sOpen = false;

struct MenuItem {
    std::string label;
    std::function<void()> action;
    /* Optional cycle handlers for value-toggle items (Display settings page).
     * When set, Left/Right invoke them and the renderer prefers labelFn over
     * the static label so the visible row updates with the current value. */
    std::function<void()> cycleLeft;
    std::function<void()> cycleRight;
    std::function<std::string()> labelFn;
};

struct MenuPage {
    std::string title;
    std::vector<MenuItem> items;
    int cursor = 0;
    /* Viewport: index of the topmost visible item. Renderer + key handler
     * together keep `cursor` inside [viewportTop, viewportTop + visible). */
    int viewportTop = 0;
};

/* Maximum number of items shown at once on a page. Larger pages scroll —
 * cursor still walks every item, but only a window of this many is drawn. */
constexpr int kVisibleItemsMax = 18;

std::vector<MenuPage> sPageStack;
std::string sToast;            /* Temporary message shown at bottom of screen. */
unsigned int sToastUntilTicks = 0;

/* ===================================================================== *
 *  RetroAchievements unlock overlay (issue #12)
 *
 *  A richer, animated toast for achievement unlocks — ported from the web
 *  mockup (RA - Switch). Six visual variants, selectable in the display menu
 *  and persisted (Port_Config_RaOverlayVariant). Drawn with the same SDL
 *  Renderer 2D vocabulary the menu/toast already use:
 *    - SDL_RenderFillRect  -> panels / glow layers / progress bars
 *    - SDL_SetRenderDrawColor + BLENDMODE_BLEND -> translucency (glass approx)
 *    - SDL_RenderDebugText -> labels (8x8 ASCII font, scaled on Switch)
 *
 *  What the web mockup does in CSS that we approximate here:
 *    - backdrop blur (glass)   -> dark translucent panel
 *    - box-shadow glow         -> concentric rects with decreasing alpha
 *    - SVG trophy / ring / hex -> simple rect-based glyph; ring/hex squared off
 *    - shimmer sweep (Brilho)  -> a bright vertical band swept across by time
 *  The bitmap font has no accents and no weight axis, so copy is ASCII.
 * ===================================================================== */

/* Tier palette, ported from data.js TIERS. RGB only (alpha is applied per
 * draw). Order matches the rc tier hints we map from points. */
struct RaTier {
    const char* label;
    Uint8 mr, mg, mb;   /* main accent */
    Uint8 lr, lg, lb;   /* light (highlights / text) */
    Uint8 dr, dg, db;   /* deep (trophy well) */
};
constexpr RaTier kRaTiers[4] = {
    /* bronze  */ { "Bronze",  0xC7,0x7F,0x45,  0xFB,0xD9,0xB4,  0x6E,0x42,0x20 },
    /* prata   */ { "Prata",   0xAA,0xB6,0xC6,  0xF0,0xF5,0xFC,  0x5A,0x66,0x76 },
    /* ouro    */ { "Ouro",    0xEE,0xBE,0x45,  0xFF,0xEB,0xAE,  0x8E,0x6C,0x18 },
    /* platina */ { "Platina", 0x7F,0xD6,0xE2,  0xE2,0xFB,0xFF,  0x2D,0x6E,0x7A },
};

/* A live achievement-unlock toast. Populated by Port_RA_Toast / the rich
 * unlock path; rendered each frame until it expires (then fades out). */
struct RaToast {
    bool         active = false;
    char         name[96] = {0};   /* achievement title */
    char         game[96] = {0};   /* game title */
    bool         hasPoints = false;/* true = points is real (show even if 0) */
    int          points = 0;
    int          tier = 0;         /* index into kRaTiers */
    float        rarity = 0.0f;    /* % who unlocked (0 = unknown/hide) */
    int          unlocked = 0;     /* progress numerator (0 = hide progress) */
    int          total = 0;        /* progress denominator */
    unsigned int shownAt = 0;      /* SDL_GetTicks when it appeared */
    unsigned int until = 0;        /* SDL_GetTicks when it should start leaving */
};
RaToast sRaToast;

/* Animation timing (mirrors the mockup's 0.62s in / 0.44s out easing). */
constexpr unsigned int kRaInMs  = 420;
constexpr unsigned int kRaOutMs = 360;

/* Variant display names, indexed by Port_Config_RaOverlayVariant (0..5). */
const char* const kRaVariantNames[6] = {
    "Pilula", "Cartao", "Minimo", "Brilho", "Medalha", "Vitral",
};

/* Map RetroAchievements points to a tier index, mirroring the mockup's feel
 * (more points = rarer = higher tier). rc_client gives us points, not a tier. */
static int RaTierForPoints(int pts) {
    if (pts >= 100) return 3; /* platina */
    if (pts >= 50)  return 2; /* ouro    */
    if (pts >= 25)  return 1; /* prata   */
    return 0;                 /* bronze  */
}

/* Items in sPageStack store std::function lambdas. Clearing the stack
 * inside one of those lambdas would destroy the std::function whose body
 * is currently executing — even though the executing copy is a local,
 * the implementation is fragile enough that doing it has been blamed for
 * a crash on "Close menu". Defer the actual stack clear/pop to the
 * top-level HandleKey caller via these flags. */
int sPendingPops = 0;
bool sPendingClose = false;

/* Pick the string for the current overlay language (Port_Config_Language:
 * 0 = EN, 1 = PT, 2 = ES). PT/ES strings are written WITHOUT accents on
 * purpose — the 8x8 overlay font is ASCII-only. Falls back to English. */
static const char* Tr(const char* en, const char* pt, const char* es) {
    switch (Port_Config_Language()) {
        case 1:  return pt;
        case 2:  return es;
        default: return en;
    }
}

void Toast(const std::string& msg) {
    sToast = msg;
    sToastUntilTicks = SDL_GetTicks() + 1500;
}

/* Populate + arm the rich RA unlock overlay. Shared by the structured C entry
 * point and the legacy string Toast fallback. duration is how long it stays
 * fully visible before fading. */
static void RaArmToast(const char* name, const char* game, int points, bool hasPoints,
                       float rarity, int unlocked, int total, unsigned int durationMs) {
    sRaToast = RaToast{};  /* value-init (RaToast is non-trivial) */
    std::snprintf(sRaToast.name, sizeof sRaToast.name, "%s", name ? name : "Conquista");
    std::snprintf(sRaToast.game, sizeof sRaToast.game, "%s", game ? game : "");
    sRaToast.hasPoints = hasPoints;
    sRaToast.points   = points;
    sRaToast.tier     = RaTierForPoints(points);
    sRaToast.rarity   = rarity;
    sRaToast.unlocked = unlocked;
    sRaToast.total    = total;
    unsigned int now  = SDL_GetTicks();
    sRaToast.shownAt  = now;
    sRaToast.until    = now + durationMs;
    sRaToast.active   = true;
}

/* C-linkage structured unlock entry point (issue #12 overlay). The RA event
 * handler in port_retroachievements.c calls this with the real achievement
 * fields so the overlay shows title/points/game/progress. game/rarity/progress
 * may be empty/zero when unknown — the variants hide those bits gracefully. */
extern "C" void Port_RA_ToastRich(const char* title, const char* game, int points,
                                  float rarity, int unlocked, int total) {
    /* Structured path: points is authoritative — show it even when 0. */
    RaArmToast(title, game, points, true, rarity, unlocked, total, 4500);
}

/* C-linkage toast for non-C++ TUs (RetroAchievements unlock announcements,
 * port_retroachievements.c). Legacy string form: also feeds the rich overlay
 * so even the plain "Achievement: X" path gets the new look. Strips a leading
 * "Achievement: " / "Conquista: " prefix to recover the bare title. */
extern "C" void Port_RA_Toast(const char* msg) {
    if (msg) {
        sToast = msg;
        sToastUntilTicks = SDL_GetTicks() + 4000;
        const char* title = msg;
        const char* colon = std::strchr(msg, ':');
        if (colon && colon[1]) {
            title = colon + 1;
            while (*title == ' ') ++title;
        }
        /* Legacy string path: points unknown — hide the XP block. */
        RaArmToast(title, "", 0, false, 0.0f, 0, 0, 4000);
    }
}

/* ------- Page builders (forward-declared so actions can push pages) ------- */
MenuPage BuildItemsPage(void);
MenuPage BuildWarpPage(void);
MenuPage BuildAllAreasPage(void);
MenuPage BuildAreaRoomsPage(unsigned char area);
MenuPage BuildDisplaySettingsPage(void);
MenuPage BuildSoftSlotsPage(void);
MenuPage BuildSaveStatesPage(void);
MenuPage BuildMainPage(void);

void Push(MenuPage page) {
    sPageStack.push_back(std::move(page));
}

void Pop(void) {
    /* Deferred — see sPendingPops/sPendingClose. The actual stack mutation
     * happens after the calling lambda has returned. */
    if (static_cast<int>(sPageStack.size()) - sPendingPops <= 1) {
        sPendingClose = true;
    } else {
        ++sPendingPops;
    }
}

void ApplyPendingMutations(void) {
    if (sPendingClose) {
        sPendingClose = false;
        sPendingPops = 0;
        sOpen = false;
        sPageStack.clear();
        return;
    }
    while (sPendingPops > 0 && !sPageStack.empty()) {
        sPageStack.pop_back();
        --sPendingPops;
    }
    sPendingPops = 0;
}

void DoWarp(unsigned char area, unsigned char room,
            unsigned short x = 0x80, unsigned short y = 0x80,
            unsigned char layer = 0) {
    if (!Port_DebugAction_Warp(area, room, x, y, layer)) {
        Toast("Warp ignored: not in gameplay");
        return;
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "Warp -> area 0x%02X room 0x%02X", area, room);
    Toast(buf);
    sOpen = false;
    sPageStack.clear();
}

MenuPage BuildItemsPage(void) {
    MenuPage p;
    p.title = "ITEMS";
    p.items.push_back({ "Unlock all items",      []() { Port_DebugAction_GiveAllItems(); Toast("All items granted"); } });
    p.items.push_back({ "Max heart containers",  []() { Port_DebugAction_MaxHearts();    Toast("Hearts maxed");      } });
    p.items.push_back({ "Heal to full",          []() { Port_DebugAction_HealFull();     Toast("Healed");            } });
    p.items.push_back({ "999 rupees",            []() { Port_DebugAction_MaxRupees();    Toast("999 rupees");        } });
    p.items.push_back({ "999 mysterious shells", []() { Port_DebugAction_MaxShells();    Toast("999 shells");        } });
    p.items.push_back({ "All kinstones fused",   []() { Port_DebugAction_AllKinstones(); Toast("All kinstones");     } });
    p.items.push_back({ "<- Back",               []() { Pop(); } });
    return p;
}

MenuPage BuildWarpPage(void) {
    MenuPage p;
    p.title = "WARP";
    /* Dungeon entries are lifted verbatim from src/data/screenTransitions.c
     * (the Wallmaster screen-transitions table, gWallMasterScreenTransitions)
     * — area, room, endX, endY, layer — so the warp goes through DoExitTransition
     * exactly the way a wallmaster pickup does. Layer=1 across all dungeons. */
    p.items.push_back({ "Hyrule Town",                  []() { DoWarp(AREA_HYRULE_TOWN, 0x00, 0x80, 0xC0, 1); } });
    /* #65 fix: Link's house lives in SOUTH_HYRULE_FIELD (room 0x01),
     * not Western_Woods_South (room 0x00). Local coords come from the
     * exit list in src/data/transitions.c (gExitList_HouseInteriors2_-
     * LinksHouseEntrance: WARP_TYPE_BORDER -> 0x290, 0x19c). */
    p.items.push_back({ "Hyrule Field - Link's house",  []() { DoWarp(AREA_HYRULE_FIELD, 0x01, 0x290, 0x19C, 1); } });
    p.items.push_back({ "Minish Woods",                 []() { DoWarp(AREA_MINISH_WOODS, 0x00, 0x80, 0xC0, 1); } });
    p.items.push_back({ "Minish Village",               []() { DoWarp(AREA_MINISH_VILLAGE, 0x00, 0x80, 0xC0, 1); } });
    p.items.push_back({ "Mt Crenel",                    []() { DoWarp(AREA_MT_CRENEL,    0x00, 0x80, 0xC0, 1); } });
    /* Spawn at Mountain Minish 4's coordinates from gUnk_additional_9 — a
     * known-walkable spot near the room's left side (#42/#43 repro). */
    p.items.push_back({ "Melari's Mines",               []() { DoWarp(AREA_MELARIS_MINE, 0x00, 0x80, 0x130, 1); } });
    p.items.push_back({ "Deepwood Shrine",              []() { DoWarp(AREA_DEEPWOOD_SHRINE,    0x0B, 0xa8, 0xb8, 1); } });
    /* Boss-room coords match the canonical entry transitions in
     * src/data/transitions.c / src/manager/holeManager.c rather than the
     * placeholder (0x80, 0x80) that left Link off-camera or invisible.
     * Layer matches what the room map expects (CoF boss is a hole drop
     * onto layer 2). */
    p.items.push_back({ "Deepwood Shrine - boss",       []() { DoWarp(AREA_DEEPWOOD_SHRINE_BOSS, 0x00, 0x88, 0xD8, 1); } });
    p.items.push_back({ "Cave of Flames",               []() { DoWarp(AREA_CAVE_OF_FLAMES,     0x04, 0x98, 0xa8, 1); } });
    /* Room 0x08 = Rollobite lava room (#36 — moving lava platforms).
     * Local coords come from the bug report's world (610, 3578) minus the
     * room origin (336, 3200) recorded in area_room_headers.json. */
    p.items.push_back({ "Cave of Flames - Rollobite",   []() { DoWarp(AREA_CAVE_OF_FLAMES,     0x08, 0x112, 0x17A, 1); } });
    p.items.push_back({ "Cave of Flames - boss",        []() { DoWarp(AREA_CAVE_OF_FLAMES_BOSS, 0x00, 0xC0, 0xF8, 2); } });
    p.items.push_back({ "Fortress of Winds",            []() { DoWarp(AREA_FORTRESS_OF_WINDS,  0x21, 0x78, 0xa8, 1); } });
    p.items.push_back({ "Temple of Droplets",           []() { DoWarp(AREA_TEMPLE_OF_DROPLETS, 0x03, 0x108, 0xf8, 1); } });
    p.items.push_back({ "Royal Crypt",                  []() { DoWarp(AREA_ROYAL_CRYPT,        0x08, 0x88, 0x78, 1); } });
    p.items.push_back({ "Palace of Winds",              []() { DoWarp(AREA_PALACE_OF_WINDS,    0x31, 0x238, 0x58, 1); } });
    /* #58 repro: bakery rafters at the reporter's exact spot. World pos
     * (1864, 117); room 3 origin map_x=0x60 << 4 = 0x600 → local (0x148, 0x75).
     * Area + room constants hardcoded — not yet mirrored above. */
    p.items.push_back({ "MinishRafters Bakery (#58 repro)",
                        []() { DoWarp(0x2E, 0x03, 0x148, 0x75, 1); } });
    /* #57 repro: Carlov's figurine shop. Area 0x23 = HouseInteriors3,
     * room 7 = Carlov, room header (0x00, 0x0E, 0xF0, 0xA0) → local centre
     * (0x78, 0x50). Walk into the device + insert shells to draw. */
    p.items.push_back({ "Carlov figurine shop (#57 repro)",
                        []() { DoWarp(0x23, 0x07, 0x78, 0x50, 1); } });
    p.items.push_back({ "All areas (raw, by index) ->", []() { Push(BuildAllAreasPage()); } });
    p.items.push_back({ "<- Back",                      []() { Pop(); } });
    return p;
}

/* Iterate every area slot and add an entry per area that has at least one
 * mapped room. The room headers come from the asset pipeline, so areas
 * with no extracted data (NULL_xx slots in include/area.h) won't appear.
 * Selecting an area pushes a per-area submenu listing its rooms. */
MenuPage BuildAllAreasPage(void) {
    MenuPage p;
    p.title = "WARP - all areas";
    for (unsigned int area = 0; area < 0x90; ++area) {
        unsigned char a = static_cast<unsigned char>(area);
        int count = Port_DebugQuery_AreaRoomCount(a);
        if (count <= 0) {
            continue;
        }
        const char* name = Port_DebugQuery_AreaName(a);
        char buf[80];
        if (name) {
            std::snprintf(buf, sizeof(buf), "0x%02X %s (%d)", area, name, count);
        } else {
            std::snprintf(buf, sizeof(buf), "0x%02X Area (%d rooms)", area, count);
        }
        p.items.push_back({ buf, [a]() { Push(BuildAreaRoomsPage(a)); } });
    }
    p.items.push_back({ "<- Back", []() { Pop(); } });
    return p;
}

/* Per-area room list. Each entry warps to the room with x = pixel_width/2,
 * y = pixel_height/2 — geometric centre. Not guaranteed walkable (could
 * spawn inside an obstacle) but good enough for debug; if you land on a
 * wall, just F8 → warp again to a different room. Layer defaults to 1. */
MenuPage BuildAreaRoomsPage(unsigned char area) {
    MenuPage p;
    char title[48];
    std::snprintf(title, sizeof(title), "WARP - area 0x%02X rooms", area);
    p.title = title;
    int count = Port_DebugQuery_AreaRoomCount(area);
    for (int r = 0; r < count; ++r) {
        unsigned short w = 0, h = 0;
        if (!Port_DebugQuery_RoomDimensions(area, static_cast<unsigned char>(r), &w, &h)) {
            continue;
        }
        char buf[64];
        std::snprintf(buf, sizeof(buf), "Room 0x%02X (%ux%u px)", r, w, h);
        unsigned char rr = static_cast<unsigned char>(r);
        unsigned short cx = static_cast<unsigned short>(w / 2);
        unsigned short cy = static_cast<unsigned short>(h / 2);
        p.items.push_back({ buf, [area, rr, cx, cy]() { DoWarp(area, rr, cx, cy, 1); } });
    }
    p.items.push_back({ "<- Back", []() { Pop(); } });
    return p;
}

MenuPage BuildDisplaySettingsPage(void) {
    /* Mirrors the file-select "L Settings" panel (src/fileselect.c
     * HandlePortSettingsMenu): same four knobs, same Left/Right ergonomics,
     * but reachable mid-game via F8 instead of only on the title screen.
     * Each item has a labelFn that re-reads the current value every frame
     * so the row updates immediately as you cycle. */
    MenuPage p;
    p.title = Tr("DISPLAY SETTINGS", "CONFIGURACOES", "CONFIGURACION");

#ifndef __SWITCH__
    /* Window scale is a PC-only knob. On Switch the display is a fixed
     * fullscreen framebuffer, so the "Scale" row instead drives internal
     * render scale (supersampling) — see the internalScale item below. */
    MenuItem scale;
    scale.cycleLeft  = []() { Port_PPU_CycleWindowScale(-1); };
    scale.cycleRight = []() { Port_PPU_CycleWindowScale(+1); };
    scale.labelFn = []() {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "Scale       %ux", (unsigned)Port_PPU_WindowScale());
        return std::string(buf);
    };
    p.items.push_back(std::move(scale));
#endif

    MenuItem filter;
    filter.cycleLeft  = []() { Port_PPU_CyclePresentationMode(-1); };
    filter.cycleRight = []() { Port_PPU_CyclePresentationMode(+1); };
    filter.labelFn = []() {
        const char* name = Port_PPU_PresentationModeName();
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%-11s %s", Tr("Upscale", "Ampliacao", "Reescalado"), name ? name : "?");
        return std::string(buf);
    };
    p.items.push_back(std::move(filter));

    MenuItem crtFilter;
    crtFilter.cycleLeft  = []() { Port_PPU_CycleFilter(-1); };
    crtFilter.cycleRight = []() { Port_PPU_CycleFilter(+1); };
    crtFilter.labelFn = []() {
        const char* name = Port_PPU_FilterName();
        char buf[80];
        std::snprintf(buf, sizeof(buf), "%-11s %s", Tr("CRT filter", "Filtro CRT", "Filtro CRT"), name ? name : "?");
        return std::string(buf);
    };
    p.items.push_back(std::move(crtFilter));

    MenuItem fps;
    fps.cycleLeft  = []() { Port_Config_CycleTargetFps(-1); };
    fps.cycleRight = []() { Port_Config_CycleTargetFps(+1); };
    fps.labelFn = []() {
        unsigned int v = Port_Config_TargetFps();
        char buf[32];
        if (v == 0) {
            std::snprintf(buf, sizeof(buf), "FPS         uncapped");
        } else {
            std::snprintf(buf, sizeof(buf), "FPS         %u", v);
        }
        return std::string(buf);
    };
    p.items.push_back(std::move(fps));

#ifndef __SWITCH__
    /* Fullscreen is a PC-only toggle. On Switch fullscreen is locked on (the
     * display is a fixed framebuffer; toggling it off pushed the game into a
     * corner of the TV), so the row is omitted there. */
    MenuItem fs;
    /* Fullscreen is binary, so left/right both toggle. */
    fs.cycleLeft  = []() { Port_PPU_ToggleFullscreen(); };
    fs.cycleRight = []() { Port_PPU_ToggleFullscreen(); };
    fs.labelFn = []() {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "Fullscreen  %s", Port_PPU_IsFullscreen() ? "on" : "off");
        return std::string(buf);
    };
    p.items.push_back(std::move(fs));
#endif

    MenuItem internalScale;
    internalScale.cycleLeft  = []() { Port_Config_CycleInternalScale(-1); };
    internalScale.cycleRight = []() { Port_Config_CycleInternalScale(+1); };
    internalScale.labelFn = []() {
        char buf[64];
        unsigned s = (unsigned)Port_Config_InternalScale();
#ifdef __SWITCH__
        /* On Switch this IS the "Scale" / quality knob (window scale is
         * meaningless on a fixed fullscreen framebuffer). 1x = native,
         * higher = sharper affine/rotation + better filter input. */
        std::snprintf(buf, sizeof(buf),
                      s == 1 ? Tr("Scale       %ux  (native)", "Escala      %ux  (nativo)", "Escala      %ux  (nativo)")
                             : Tr("Scale       %ux  (supersampled)", "Escala      %ux  (supersample)", "Escala      %ux  (supersample)"),
                      s);
#else
        /* Affine OAM is sub-pixel at scale > 1; everything else is S*S
         * replicate. Affine BG2 / mode 7 are still TODO. */
        std::snprintf(buf, sizeof(buf),
                      s == 1 ? "Internal    %ux  (off)"
                             : "Internal    %ux  (affine OBJ sub-pixel)",
                      s);
#endif
        return std::string(buf);
    };
    p.items.push_back(std::move(internalScale));

    /* Overlay language (EN/PT). Placed near the top so it's easy to find;
     * binary toggle. PT labels in the overlay are accent-free (ASCII font). */
    {
        MenuItem lang;
        lang.cycleLeft  = []() { Port_Config_CycleLanguage(-1); };
        lang.cycleRight = []() { Port_Config_CycleLanguage(+1); };
        lang.labelFn = []() {
            int l = Port_Config_Language();
            const char* name = (l == 1) ? "Portugues" : (l == 2) ? "Espanol" : "English";
            char buf[40];
            std::snprintf(buf, sizeof(buf), "%s  %s", Tr("Language", "Idioma", "Idioma"), name);
            return std::string(buf);
        };
        p.items.push_back(std::move(lang));
    }

    MenuItem fpsCounter;
    /* Binary toggle, so left/right both flip it. */
    fpsCounter.cycleLeft  = []() { Port_Config_ToggleShowFps(); };
    fpsCounter.cycleRight = []() { Port_Config_ToggleShowFps(); };
    fpsCounter.labelFn = []() {
        char buf[40];
        std::snprintf(buf, sizeof(buf), "%s %s", Tr("FPS counter", "Contador FPS", "Contador FPS"),
                      Port_Config_ShowFps() ? Tr("on", "lig", "act") : Tr("off", "des", "des"));
        return std::string(buf);
    };
    p.items.push_back(std::move(fpsCounter));

    /* FPS counter placement (issue #5): cycle through the 4 corners. */
    {
        MenuItem fpsPos;
        fpsPos.cycleLeft  = []() { Port_Config_CycleFpsCorner(-1); };
        fpsPos.cycleRight = []() { Port_Config_CycleFpsCorner(+1); };
        fpsPos.labelFn = []() {
            int c = Port_Config_FpsCorner();
            if (c < 0 || c > 3) c = 0;
            const char* corner =
                c == 0 ? Tr("top-left", "sup-esq", "sup-izq") :
                c == 1 ? Tr("top-right", "sup-dir", "sup-der") :
                c == 2 ? Tr("bottom-left", "inf-esq", "inf-izq") :
                         Tr("bottom-right", "inf-dir", "inf-der");
            char buf[48];
            std::snprintf(buf, sizeof(buf), "%s %s", Tr("FPS position", "Posicao FPS", "Posicion FPS"), corner);
            return std::string(buf);
        };
        p.items.push_back(std::move(fpsPos));
    }

    /* FPS counter size (issue #6): cycle the extra size multiplier 1x..4x. */
    {
        MenuItem fpsSize;
        fpsSize.cycleLeft  = []() { Port_Config_CycleFpsScale(-1); };
        fpsSize.cycleRight = []() { Port_Config_CycleFpsScale(+1); };
        fpsSize.labelFn = []() {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%s %dx", Tr("FPS size", "Tamanho FPS", "Tamano FPS"),
                          Port_Config_FpsScale());
            return std::string(buf);
        };
        p.items.push_back(std::move(fpsSize));
    }

    /* Dark panel behind the FPS counter (legibility). Binary toggle. */
    {
        MenuItem fpsBg;
        fpsBg.cycleLeft  = []() { Port_Config_ToggleFpsBackground(); };
        fpsBg.cycleRight = []() { Port_Config_ToggleFpsBackground(); };
        fpsBg.labelFn = []() {
            char buf[40];
            std::snprintf(buf, sizeof(buf), "%s %s", Tr("FPS background", "Fundo FPS", "Fondo FPS"),
                          Port_Config_FpsBackground() ? Tr("on", "lig", "act") : Tr("off", "des", "des"));
            return std::string(buf);
        };
        p.items.push_back(std::move(fpsBg));
    }

#ifdef __SWITCH__
    /* RetroAchievements unlock overlay style (issue #12). Cycle through the 6
     * ported variants; Left/Right also previews the choice by firing a test
     * toast so you see it immediately as you scroll. */
    {
        MenuItem raStyle;
        auto preview = [](int dir) {
            Port_Config_CycleRaOverlayVariant(dir);
            Port_RA_SimulateUnlock();
        };
        raStyle.cycleLeft  = [preview]() { preview(-1); };
        raStyle.cycleRight = [preview]() { preview(+1); };
        raStyle.labelFn = []() {
            int v = Port_Config_RaOverlayVariant();
            if (v < 0 || v > 5) v = 1;
            char buf[48];
            std::snprintf(buf, sizeof(buf), "%s %s",
                          Tr("RA overlay", "Overlay RA", "Overlay RA"), kRaVariantNames[v]);
            return std::string(buf);
        };
        p.items.push_back(std::move(raStyle));
    }
#endif

    /* Save states — TEMPORARILY DISABLED. The in-memory snapshot still crashes
     * on the save→load→cross-room path (script-context side-table gap fixed, but
     * not yet verified end-to-end on hardware). Hidden from the menu until the
     * save-state work is validated. See docs/Features/SaveStates.md for status
     * and the remaining plan. Re-enable by uncommenting this block.
     *
     * {
     *     MenuItem ss;
     *     ss.action = []() { Push(BuildSaveStatesPage()); };
     *     ss.labelFn = []() { return std::string(Tr("Save states  >", "Save states  >", "Save states  >")); };
     *     p.items.push_back(std::move(ss));
     * }
     */

#ifdef __SWITCH__
    /* RetroAchievements (issue #12): user-triggered login. Opens the native
     * keyboard for username/password; the label reflects whether we're logged
     * in. Softcore only. Functions live in port_retroachievements.c. */
    {
        MenuItem ra;
        ra.action = []() {
            if (Port_RA_IsLoggedIn()) {
                Toast(Tr("Already logged in", "Ja conectado", "Ya conectado"));
            } else {
                Toast(Tr("Opening keyboard...", "Abrindo teclado...", "Abriendo teclado..."));
                Port_RA_InteractiveLogin();
            }
        };
        ra.labelFn = []() {
            if (Port_RA_IsLoggedIn()) {
                const char* u = Port_RA_UserName();
                return std::string(Tr("RetroAchievements: ", "RetroAchievements: ", "RetroAchievements: "))
                       + (u ? u : "?");
            }
            return std::string(Tr("RetroAchievements: log in", "RetroAchievements: entrar",
                                  "RetroAchievements: entrar"));
        };
        p.items.push_back(std::move(ra));

        /* Debug (issue #25): fire the unlock toast without playing/server, to
         * iterate the achievement overlay (#21/#22/#24). */
        MenuItem raTest;
        raTest.action = []() { Port_RA_SimulateUnlock(); };
        raTest.labelFn = []() {
            return std::string(Tr("RA: test unlock toast", "RA: testar toast",
                                  "RA: probar toast"));
        };
        p.items.push_back(std::move(raTest));
    }
#endif

    {
        MenuItem back;
        back.action = []() { Pop(); };
        back.labelFn = []() { return std::string(Tr("<- Back", "<- Voltar", "<- Atras")); };
        p.items.push_back(std::move(back));
    }
    return p;
}

/* Soft-slot assignment page. Each row is a cycle item: Left/Right walks
 * through the items the player owns. The label is regenerated every frame
 * via labelFn, so the displayed assignment updates immediately on cycle. */
MenuPage BuildSoftSlotsPage(void) {
    MenuPage p;
    p.title = "EXTRA EQUIP SLOTS";
    for (int s = 0; s < 4; ++s) {
        MenuItem it;
        it.cycleLeft  = [s]() { Port_SoftSlots_CycleAssignment(s, -1); };
        it.cycleRight = [s]() { Port_SoftSlots_CycleAssignment(s, +1); };
        it.labelFn = [s]() { return std::string(Port_SoftSlots_GetSlotLabel(s)); };
        p.items.push_back(std::move(it));
    }
    p.items.push_back({ "<- Back", []() { Pop(); } });
    return p;
}

/* Per-slot save-state page. One Enter to save, one to load — the extra
 * step (vs. the row itself triggering an action) is the guard against a
 * stray gamepad press clobbering or overwriting the live game. */
MenuPage BuildSaveStateSlotPage(int slot) {
    MenuPage p;
    char title[32];
    std::snprintf(title, sizeof(title), "SAVE STATE - SLOT %d", slot + 1);
    p.title = title;

    {
        MenuItem save;
        save.action = [slot]() {
            if (Port_QuickSave_Slot(slot)) {
                char buf[48];
                std::snprintf(buf, sizeof(buf), "%s %d", Tr("Saved slot", "Slot salvo", "Slot guardado"), slot + 1);
                Toast(buf);
            } else if (!Port_QuickSave_CanSave()) {
                /* The state guard rejected it — saving in a transition would
                 * capture a half-built entity graph that crashes on load. */
                Toast(Tr("Can only save during gameplay", "So pode salvar durante o jogo", "Solo se guarda durante el juego"));
            } else {
                Toast(Tr("Save failed", "Falha ao salvar", "Error al guardar"));
            }
        };
        save.labelFn = []() { return std::string(Tr("Save to this slot", "Salvar neste slot", "Guardar en este slot")); };
        p.items.push_back(std::move(save));
    }

    MenuItem load;
    load.action = [slot]() {
        if (!Port_QuickSave_SlotHasSnapshot(slot)) {
            Toast(Tr("Empty slot", "Slot vazio", "Slot vacio"));
            return;
        }
        if (Port_QuickLoad_Slot(slot)) {
            /* The restore itself happens at the next frame boundary (deferred
             * for safety). Close the menu so the player sees the result. */
            char buf[48];
            std::snprintf(buf, sizeof(buf), "%s %d", Tr("Loaded slot", "Slot carregado", "Slot cargado"), slot + 1);
            Toast(buf);
            Pop(); /* close slot page */
            Pop(); /* close save-states page */
        } else {
            Toast(Tr("Load failed", "Falha ao carregar", "Error al cargar"));
        }
    };
    load.labelFn = [slot]() {
        bool has = Port_QuickSave_SlotHasSnapshot(slot);
        return std::string(has ? Tr("Load from this slot", "Carregar deste slot", "Cargar de este slot")
                               : Tr("Load from this slot (empty)", "Carregar deste slot (vazio)", "Cargar de este slot (vacio)"));
    };
    p.items.push_back(std::move(load));

    {
        MenuItem back;
        back.action = []() { Pop(); };
        back.labelFn = []() { return std::string(Tr("<- Back", "<- Voltar", "<- Atras")); };
        p.items.push_back(std::move(back));
    }
    return p;
}

/* Save-states overview — one row per slot showing empty/occupied, so the
 * player can see at a glance where to save and where to load. Reachable on
 * Switch (no F5/F6 keys) because it is linked from the display-settings page
 * that L+R opens, as well as from the PC F8 main page. */
MenuPage BuildSaveStatesPage(void) {
    MenuPage p;
    p.title = "SAVE STATES";
    int count = Port_QuickSave_SlotCount();
    for (int s = 0; s < count; ++s) {
        MenuItem it;
        it.action = [s]() { Push(BuildSaveStateSlotPage(s)); };
        it.labelFn = [s]() {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "Slot %d: %s", s + 1,
                          Port_QuickSave_SlotHasSnapshot(s) ? Tr("used", "ocupado", "ocupado")
                                                            : Tr("empty", "vazio", "vacio"));
            return std::string(buf);
        };
        p.items.push_back(std::move(it));
    }
    {
        MenuItem back;
        back.action = []() { Pop(); };
        back.labelFn = []() { return std::string(Tr("<- Back", "<- Voltar", "<- Atras")); };
        p.items.push_back(std::move(back));
    }
    return p;
}

MenuPage BuildMainPage(void) {
    MenuPage p;
    p.title = "DEBUG MENU (F8 to close)";
    p.items.push_back({ "Items / progress",  []() { Push(BuildItemsPage()); } });
    p.items.push_back({ "Warp",              []() { Push(BuildWarpPage());  } });
    p.items.push_back({ "Display settings",  []() { Push(BuildDisplaySettingsPage()); } });
    /* Save states temporarily disabled — see docs/Features/SaveStates.md.
     * p.items.push_back({ "Save states",       []() { Push(BuildSaveStatesPage()); } }); */
    p.items.push_back({ "Extra equip slots", []() { Push(BuildSoftSlotsPage()); } });
    p.items.push_back({ "Heal to full",      []() { Port_DebugAction_HealFull(); Toast("Healed"); } });
    p.items.push_back({ "Close menu",        []() { Pop(); } });
    return p;
}

} /* namespace */

/* ============================================================ */
/*                          Public API                          */
/* ============================================================ */

extern "C" void Port_DebugMenu_Toggle(void) {
    if (sOpen) {
        sOpen = false;
        sPageStack.clear();
    } else {
        sOpen = true;
        sPageStack.clear();
        sPageStack.push_back(BuildMainPage());
    }
}

/* Open the overlay directly on the display-settings page (no cheats/warps).
 * Used by the Switch L+R shortcut so the quick-settings panel is reachable on
 * the file-select, name-entry and in-game screens. Calling it while already
 * open is a no-op so the L+R edge in the caller can treat "open" idempotently. */
extern "C" void Port_DebugMenu_OpenSettings(void) {
    if (sOpen) {
        return;
    }
    sOpen = true;
    sPageStack.clear();
    sPageStack.push_back(BuildDisplaySettingsPage());
}

extern "C" bool Port_DebugMenu_IsOpen(void) {
    return sOpen;
}

extern "C" bool Port_DebugMenu_HandleKey(int sdlKey) {
    if (!sOpen || sPageStack.empty()) {
        return false;
    }
    bool consumed = false;
    {
        MenuPage& page = sPageStack.back();
        int n = static_cast<int>(page.items.size());

        auto clampViewport = [&]() {
            int visible = std::min(n, kVisibleItemsMax);
            if (page.cursor < page.viewportTop) {
                page.viewportTop = page.cursor;
            } else if (page.cursor >= page.viewportTop + visible) {
                page.viewportTop = page.cursor - visible + 1;
            }
            if (page.viewportTop < 0) {
                page.viewportTop = 0;
            }
            if (page.viewportTop + visible > n) {
                page.viewportTop = std::max(0, n - visible);
            }
        };

        switch (sdlKey) {
            case SDLK_UP:
                if (n > 0) {
                    page.cursor = (page.cursor - 1 + n) % n;
                    clampViewport();
                }
                consumed = true;
                break;
            case SDLK_DOWN:
                if (n > 0) {
                    page.cursor = (page.cursor + 1) % n;
                    clampViewport();
                }
                consumed = true;
                break;
            case SDLK_PAGEUP:
                if (n > 0) {
                    page.cursor = std::max(0, page.cursor - kVisibleItemsMax);
                    clampViewport();
                }
                consumed = true;
                break;
            case SDLK_PAGEDOWN:
                if (n > 0) {
                    page.cursor = std::min(n - 1, page.cursor + kVisibleItemsMax);
                    clampViewport();
                }
                consumed = true;
                break;
            case SDLK_HOME:
                if (n > 0) {
                    page.cursor = 0;
                    clampViewport();
                }
                consumed = true;
                break;
            case SDLK_END:
                if (n > 0) {
                    page.cursor = n - 1;
                    clampViewport();
                }
                consumed = true;
                break;
            case SDLK_RETURN:
            case SDLK_KP_ENTER:
            case SDLK_SPACE:
                if (page.cursor >= 0 && page.cursor < n) {
                    /* Copy the function so the std::function we're calling
                     * stays alive even if the page (and its items) get
                     * popped/cleared inside the lambda. For cycle items,
                     * Enter behaves like Right (forward cycle). */
                    auto& it = page.items[page.cursor];
                    auto fn = it.action ? it.action : it.cycleRight;
                    if (fn) fn();
                }
                consumed = true;
                break;
            case SDLK_LEFT:
                if (page.cursor >= 0 && page.cursor < n) {
                    auto fn = page.items[page.cursor].cycleLeft;
                    if (fn) fn();
                }
                consumed = true;
                break;
            case SDLK_RIGHT:
                if (page.cursor >= 0 && page.cursor < n) {
                    auto fn = page.items[page.cursor].cycleRight;
                    if (fn) fn();
                }
                consumed = true;
                break;
            case SDLK_ESCAPE:
            case SDLK_BACKSPACE:
                Pop();
                consumed = true;
                break;
            default:
                break;
        }
        /* `page` reference must not be used after this scope ends — the
         * pending-mutation step below may invalidate it. */
    }
    ApplyPendingMutations();
    return consumed;
}

/* Effective glyph width. On Switch the debug font is drawn scaled (see
 * sdl3compat_DebugTextScale) so the overlay geometry must use the same scaled
 * cell or the box and text drift apart. On other targets it is the raw 8px. */
static int Port_DebugMenu_CharW(SDL_Renderer* renderer) {
#ifdef __SWITCH__
    extern int sdl3compat_DebugTextScale(SDL_Renderer*);
    return SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE * sdl3compat_DebugTextScale(renderer);
#else
    (void)renderer;
    return SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE;
#endif
}

/* ===================================================================== *
 *  RA overlay — drawing helpers + the six variants.
 *
 *  Everything is sized off `u`, a single unit derived from the glyph cell, so
 *  the overlay scales with the display the same way the menu does. Geometry
 *  numbers come from the mockup's cqh/cqw values, rescaled to the 8x8 font.
 * ===================================================================== */

static inline SDL_FRect RaRect(float x, float y, float w, float h) {
    SDL_FRect r = { x, y, w, h };
    return r;
}

static void RaFill(SDL_Renderer* r, SDL_FRect rect, Uint8 cr, Uint8 cg, Uint8 cb, Uint8 ca) {
    SDL_SetRenderDrawColor(r, cr, cg, cb, ca);
    SDL_RenderFillRect(r, &rect);
}

/* Glass panel: dark translucent fill + a 1px light top edge (the inset
 * highlight the mockup's .glass uses) + a faint border. Approximates blur. */
static void RaGlassPanel(SDL_Renderer* r, SDL_FRect box, Uint8 borderAlpha) {
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    RaFill(r, box, 22, 24, 32, 214);                 /* body */
    RaFill(r, RaRect(box.x, box.y, box.w, 1.0f), 255, 255, 255, 46); /* top sheen */
    SDL_SetRenderDrawColor(r, 255, 255, 255, borderAlpha);
    SDL_RenderRect(r, &box);
}

/* Accent glow: concentric outlines around `box` in the tier colour, fading
 * out — the rect-based stand-in for the mockup's box-shadow glow. */
static void RaGlow(SDL_Renderer* r, SDL_FRect box, const RaTier& t, int layers, float step) {
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    for (int i = layers; i >= 1; --i) {
        float g = step * i;
        SDL_FRect o = RaRect(box.x - g, box.y - g, box.w + 2*g, box.h + 2*g);
        Uint8 a = (Uint8)(70 / (i + 1));
        SDL_SetRenderDrawColor(r, t.mr, t.mg, t.mb, a);
        SDL_RenderRect(r, &o);
    }
}

/* Trophy glyph: a rounded well (radial-ish via two stacked rects) with a cup
 * shape built from rects, tinted by tier. No SVG — a compact rect mosaic that
 * reads as a trophy at overlay scale. `s` is the cell side. */
static void RaTrophy(SDL_Renderer* r, float x, float y, float s, const RaTier& t) {
    /* well background */
    RaFill(r, RaRect(x, y, s, s), t.dr, t.dg, t.db, 235);
    RaFill(r, RaRect(x, y, s, s*0.45f), t.dr/2, t.dg/2, t.db/2, 120);
    float cx = x + s * 0.5f;
    float cupW = s * 0.50f, cupH = s * 0.34f;
    /* cup bowl */
    RaFill(r, RaRect(cx - cupW*0.5f, y + s*0.22f, cupW, cupH), t.mr, t.mg, t.mb, 255);
    /* highlight */
    RaFill(r, RaRect(cx - cupW*0.5f, y + s*0.22f, cupW*0.34f, cupH*0.8f), t.lr, t.lg, t.lb, 200);
    /* handles */
    RaFill(r, RaRect(cx - cupW*0.5f - s*0.10f, y + s*0.26f, s*0.08f, cupH*0.6f), t.mr, t.mg, t.mb, 230);
    RaFill(r, RaRect(cx + cupW*0.5f + s*0.02f, y + s*0.26f, s*0.08f, cupH*0.6f), t.mr, t.mg, t.mb, 230);
    /* stem + base */
    RaFill(r, RaRect(cx - s*0.05f, y + s*0.56f, s*0.10f, s*0.12f), t.db, t.dg, t.db, 255);
    RaFill(r, RaRect(cx - cupW*0.45f, y + s*0.68f, cupW*0.9f, s*0.10f), t.mr, t.mg, t.mb, 235);
}

/* Progress bar: track + tier-filled portion + "n/total" text to the right. */
static void RaProgressBar(SDL_Renderer* r, float x, float y, float w, float h,
                          int unlocked, int total, const RaTier& t, int charW) {
    RaFill(r, RaRect(x, y, w, h), 255, 255, 255, 30);
    if (total > 0) {
        float pct = (float)unlocked / (float)total;
        if (pct < 0.0f) pct = 0.0f;
        if (pct > 1.0f) pct = 1.0f;
        RaFill(r, RaRect(x, y, w * pct, h), t.mr, t.mg, t.mb, 255);
    }
    char buf[24];
    std::snprintf(buf, sizeof buf, "%d/%d", unlocked, total);
    SDL_SetRenderDrawColor(r, 220, 220, 230, 255);
    SDL_RenderDebugText(r, x + w + charW * 0.6f, y + (h - charW) * 0.5f, buf);
}

static void RaText(SDL_Renderer* r, float x, float y, const char* s,
                   Uint8 cr, Uint8 cg, Uint8 cb, Uint8 ca = 255) {
    SDL_SetRenderDrawColor(r, cr, cg, cb, ca);
    SDL_RenderDebugText(r, x, y, s);
}

/* Eased progress 0..1 of the in/out animation. Returns alpha [0..1] and a
 * slide offset (in px) applied along the variant's entry direction. dirLeft
 * slides from the left edge; otherwise from the top. */
struct RaAnim { float alpha; float dx; float dy; };
static RaAnim RaComputeAnim(const RaToast& to, bool dirLeft, float slidePx, unsigned int now) {
    RaAnim a = { 1.0f, 0.0f, 0.0f };
    if (now < to.shownAt + kRaInMs) {
        float p = (float)(now - to.shownAt) / (float)kRaInMs;
        float e = 1.0f - (1.0f - p) * (1.0f - p) * (1.0f - p); /* ease-out cubic */
        a.alpha = e;
        float off = (1.0f - e) * slidePx;
        if (dirLeft) a.dx = -off; else a.dy = -off;
    } else if (now >= to.until) {
        float p = (float)(now - to.until) / (float)kRaOutMs;
        if (p > 1.0f) p = 1.0f;
        a.alpha = 1.0f - p;
        float off = p * slidePx * 0.5f;
        if (dirLeft) a.dx = -off; else a.dy = -off;
    }
    return a;
}

/* Each variant draws into the box at (ox,oy). They share the atoms above and
 * differ in layout / chrome, matching VPilula..VVitral in overlays.jsx. */

static void RaDrawPilula(SDL_Renderer* r, float ox, float oy, const RaToast& to,
                         const RaTier& t, int charW, Uint8 A) {
    float pad = charW * 0.9f;
    float ts = charW * 2.6f;
    float bodyX = ox + pad + ts + charW;
    int nameLen = (int)std::strlen(to.name);
    int gameLen = (int)std::strlen(to.game);
    float textW = (float)std::max(nameLen, gameLen + 8) * charW;
    float w = pad + ts + charW + textW + charW * 5.0f + pad;
    float h = ts + pad * 2.0f;
    SDL_FRect box = RaRect(ox, oy, w, h);
    RaGlassPanel(r, box, (Uint8)(36 * A / 255));
    RaTrophy(r, ox + pad, oy + pad, ts, t);
    RaText(r, bodyX, oy + pad, "CONQUISTA DESBLOQUEADA", t.lr, t.lg, t.lb, A);
    RaText(r, bodyX, oy + pad + charW * 1.4f, to.name, 255, 255, 255, A);
    RaText(r, bodyX, oy + pad + charW * 2.8f, to.game, 190, 195, 205, A);
    /* XP ("50 XP"), one line, right-aligned. Hidden when unknown (0 pts). */
    if (to.hasPoints) {
        char xp[24]; std::snprintf(xp, sizeof xp, "%d XP", to.points);
        float xpW = (float)std::strlen(xp) * charW;
        RaText(r, ox + w - pad - xpW, oy + h * 0.5f - charW * 0.5f, xp, t.lr, t.lg, t.lb, A);
    }
}

static void RaDrawCartao(SDL_Renderer* r, float ox, float oy, const RaToast& to,
                         const RaTier& t, int charW, Uint8 A) {
    float pad = charW * 1.0f;
    float ts = charW * 3.0f;
    int nameLen = (int)std::strlen(to.name);
    int gameLen = (int)std::strlen(to.game);
    float textW = (float)std::max(nameLen, gameLen) * charW;
    float w = pad + ts + charW + textW + charW * 6.0f + pad;
    if (w < charW * 34) w = charW * 34;
    float topH = ts;
    float footH = charW * 2.4f;
    float h = pad + topH + charW * 1.2f + footH + pad;
    SDL_FRect box = RaRect(ox, oy, w, h);
    RaGlassPanel(r, box, (Uint8)(36 * A / 255));
    /* top row */
    RaTrophy(r, ox + pad, oy + pad, ts, t);
    float bodyX = ox + pad + ts + charW;
    char kick[40]; std::snprintf(kick, sizeof kick, "CONQUISTA - %s", t.label);
    RaText(r, bodyX, oy + pad, kick, t.lr, t.lg, t.lb, A);
    RaText(r, bodyX, oy + pad + charW * 1.4f, to.name, 255, 255, 255, A);
    RaText(r, bodyX, oy + pad + charW * 2.8f, to.game, 190, 195, 205, A);
    /* XP, single line ("+50 XP"), right-aligned. Hidden when unknown (0 pts). */
    if (to.hasPoints) {
        char xp[24]; std::snprintf(xp, sizeof xp, "+%d XP", to.points);
        float xpW = (float)std::strlen(xp) * charW;
        RaText(r, ox + w - pad - xpW, oy + pad, xp, t.lr, t.lg, t.lb, A);
    }
    /* divider */
    float footY = oy + pad + topH + charW * 0.6f;
    RaFill(r, RaRect(ox + pad, footY, w - pad * 2.0f, 1.0f), 255, 255, 255, (Uint8)(26 * A / 255));
    /* foot: progress + rarity */
    float fY = footY + charW * 0.8f;
    if (to.total > 0)
        RaProgressBar(r, ox + pad, fY, w * 0.40f, charW * 0.9f, to.unlocked, to.total, t, charW);
    if (to.rarity > 0.0f) {
        char rar[40]; std::snprintf(rar, sizeof rar, "%.1f%% desbloquearam", to.rarity);
        float rx = ox + w - pad - (float)std::strlen(rar) * charW - charW * 1.4f;
        RaFill(r, RaRect(rx, fY + charW * 0.1f, charW * 0.7f, charW * 0.7f), t.mr, t.mg, t.mb, A);
        RaText(r, rx + charW * 1.2f, fY, rar, 200, 205, 215, A);
    }
}

static void RaDrawMinimo(SDL_Renderer* r, float ox, float oy, const RaToast& to,
                         const RaTier& t, int charW, Uint8 A) {
    float pad = charW * 0.7f;
    float ts = charW * 1.6f;
    char line[200];
    std::snprintf(line, sizeof line, "%s", to.name);
    int len = (int)std::strlen(line) + (int)std::strlen(to.game) + 12;
    float w = pad + ts + charW * 0.6f + (float)len * charW + pad;
    float h = ts + pad * 2.0f;
    SDL_FRect box = RaRect(ox, oy, w, h);
    RaGlassPanel(r, box, (Uint8)(30 * A / 255));
    RaTrophy(r, ox + pad, oy + pad, ts, t);
    float tx = ox + pad + ts + charW * 0.6f;
    float ty = oy + h * 0.5f - charW * 0.5f;
    RaText(r, tx, ty, to.name, 255, 255, 255, A);
    float gx = tx + (float)std::strlen(to.name) * charW + charW;
    RaFill(r, RaRect(gx - charW * 0.4f, ty + charW * 0.3f, charW * 0.35f, charW * 0.35f), 200, 200, 210, (Uint8)(140 * A / 255));
    RaText(r, gx + charW * 0.4f, ty, to.game, 175, 180, 190, A);
    char xp[16]; std::snprintf(xp, sizeof xp, "+%d", to.points);
    RaText(r, ox + w - pad - (float)std::strlen(xp) * charW, ty, xp, t.lr, t.lg, t.lb, A);
}

static void RaDrawBrilho(SDL_Renderer* r, float ox, float oy, const RaToast& to,
                         const RaTier& t, int charW, Uint8 A, unsigned int now) {
    float pad = charW * 0.9f;
    float ts = charW * 2.8f;
    float bodyX = ox + pad + ts + charW;
    int nameLen = (int)std::strlen(to.name);
    int gameLen = (int)std::strlen(to.game);
    float textW = (float)std::max(nameLen, gameLen + 4) * charW;
    float w = pad + ts + charW + textW + charW * 5.0f + pad;
    float h = ts + pad * 2.0f;
    SDL_FRect box = RaRect(ox, oy, w, h);
    RaGlow(r, box, t, 4, charW * 0.5f);
    RaGlassPanel(r, box, (Uint8)(80 * A / 255));
    /* shimmer band sweeping across, 2.6s loop */
    float phase = (float)((now) % 2600) / 2600.0f;
    float bandW = w * 0.18f;
    float bx = ox - bandW + (w + bandW) * phase;
    SDL_FRect band = RaRect(bx, oy, bandW, h);
    /* clip to box by intersecting manually (cheap: only draw if inside) */
    if (bx + bandW > ox && bx < ox + w) {
        RaFill(r, band, 255, 255, 255, (Uint8)(38 * A / 255));
    }
    RaTrophy(r, ox + pad, oy + pad, ts, t);
    char kick[24]; std::snprintf(kick, sizeof kick, "* %s", t.label);
    RaText(r, bodyX, oy + pad, kick, t.lr, t.lg, t.lb, A);
    RaText(r, bodyX, oy + pad + charW * 1.4f, to.name, 255, 255, 255, A);
    RaText(r, bodyX, oy + pad + charW * 2.8f, to.game, 195, 200, 210, A);
    if (to.hasPoints) {
        char xp[24]; std::snprintf(xp, sizeof xp, "%d pts", to.points);
        float xpW = (float)std::strlen(xp) * charW;
        RaText(r, ox + w - pad - xpW, oy + h * 0.5f - charW * 0.5f, xp, t.lr, t.lg, t.lb, A);
    }
}

static void RaDrawMedalha(SDL_Renderer* r, float ox, float oy, const RaToast& to,
                          const RaTier& t, int charW, Uint8 A) {
    /* Circular medallion approximated as a square medal with a tier "ring"
     * frame (the SDL_Renderer has no arc; squared off, as agreed). */
    float md = charW * 4.2f;
    /* ring frame */
    RaFill(r, RaRect(ox, oy, md, md), t.mr, t.mg, t.mb, A);
    RaFill(r, RaRect(ox + md*0.12f, oy + md*0.12f, md*0.76f, md*0.76f), 16, 18, 24, 255);
    RaTrophy(r, ox + md*0.22f, oy + md*0.22f, md*0.56f, t);
    /* body card, overlapping the medal slightly */
    float cardX = ox + md - charW * 0.8f;
    float pad = charW * 0.9f;
    int nameLen = (int)std::strlen(to.name);
    int gameLen = (int)std::strlen(to.game);
    float textW = (float)std::max(nameLen, gameLen) * charW + charW * 4.0f;
    float w = textW + pad * 2.0f;
    float h = md;
    SDL_FRect card = RaRect(cardX, oy + md*0.10f, w, h - md*0.20f);
    RaGlassPanel(r, card, (Uint8)(36 * A / 255));
    float tx = cardX + pad + charW;
    RaText(r, tx, card.y + pad * 0.6f, "CONQUISTA DESBLOQUEADA", t.lr, t.lg, t.lb, A);
    RaText(r, tx, card.y + pad * 0.6f + charW * 1.4f, to.name, 255, 255, 255, A);
    RaText(r, tx, card.y + pad * 0.6f + charW * 2.8f, to.game, 190, 195, 205, A);
    char meta[48];
    std::snprintf(meta, sizeof meta, "%.1f%% - +%d XP", to.rarity, to.points);
    RaText(r, tx, card.y + card.h - pad * 0.6f - charW, meta, 200, 205, 215, A);
}

static void RaDrawVitral(SDL_Renderer* r, float ox, float oy, const RaToast& to,
                         const RaTier& t, int charW, Uint8 A) {
    float pad = charW * 0.9f;
    float ts = charW * 2.8f;
    float bodyX = ox + pad + ts + charW;
    int nameLen = (int)std::strlen(to.name);
    int gameLen = (int)std::strlen(to.game);
    float textW = (float)std::max(nameLen, gameLen + 6) * charW;
    float w = pad + ts + charW + textW + charW * 5.0f + pad;
    float h = ts + pad * 2.0f;
    SDL_FRect box = RaRect(ox, oy, w, h);
    RaGlassPanel(r, box, 0);
    /* gradient-ish hairline: top edge in light tier, bottom in deep tier
     * (the mockup's vitral-edge mask, squared to two coloured borders). */
    RaFill(r, RaRect(box.x, box.y, box.w, 1.5f), t.lr, t.lg, t.lb, A);
    RaFill(r, RaRect(box.x, box.y + box.h - 1.5f, box.w, 1.5f), t.dr, t.dg, t.db, A);
    RaFill(r, RaRect(box.x, box.y, 1.5f, box.h), t.lr, t.lg, t.lb, (Uint8)(170 * A / 255));
    RaFill(r, RaRect(box.x + box.w - 1.5f, box.y, 1.5f, box.h), t.dr, t.dg, t.db, (Uint8)(170 * A / 255));
    /* hex trophy well -> diamond-ish: draw the trophy on a tinted plate */
    RaFill(r, RaRect(ox + pad, oy + pad, ts, ts), t.dr/2, t.dg/2, t.db/2, A);
    RaTrophy(r, ox + pad, oy + pad, ts, t);
    char kick[48]; std::snprintf(kick, sizeof kick, "%s - %s", to.game, t.label);
    RaText(r, bodyX, oy + pad, kick, t.lr, t.lg, t.lb, A);
    RaText(r, bodyX, oy + pad + charW * 1.4f, to.name, 255, 255, 255, A);
    RaText(r, bodyX, oy + pad + charW * 2.8f, to.game, 190, 195, 205, A);
    if (to.hasPoints) {
        char xp[24]; std::snprintf(xp, sizeof xp, "%d XP", to.points);
        float xpW = (float)std::strlen(xp) * charW;
        RaText(r, ox + w - pad - xpW, oy + h * 0.5f - charW * 0.5f, xp, t.lr, t.lg, t.lb, A);
    }
}

/* Render the active RA unlock toast (if any), top-left like the mockup's
 * toast-zone. Returns true if something was drawn. Variants 1 (Cartao) and 4
 * (Medalha) slide from the left; the rest from the top, matching VARIANTS.dir. */
static bool RaRenderToast(SDL_Renderer* r, int winW, int winH, int charW) {
    (void)winW; (void)winH;
    if (!sRaToast.active) return false;
    unsigned int now = SDL_GetTicks();
    if (now >= sRaToast.until + kRaOutMs) {
        sRaToast.active = false;
        return false;
    }
    int v = Port_Config_RaOverlayVariant();
    if (v < 0 || v > 5) v = 1;
    const RaTier& t = kRaTiers[(sRaToast.tier >= 0 && sRaToast.tier <= 3) ? sRaToast.tier : 0];

    bool dirLeft = (v == 1 || v == 4);
    float slide = dirLeft ? charW * 30.0f : charW * 10.0f;
    RaAnim an = RaComputeAnim(sRaToast, dirLeft, slide, now);
    Uint8 A = (Uint8)(255.0f * (an.alpha < 0 ? 0 : an.alpha > 1 ? 1 : an.alpha));

    float ox = charW * 2.0f + an.dx;
    float oy = charW * 2.0f + an.dy;

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    switch (v) {
        case 0: RaDrawPilula(r, ox, oy, sRaToast, t, charW, A); break;
        case 1: RaDrawCartao(r, ox, oy, sRaToast, t, charW, A); break;
        case 2: RaDrawMinimo(r, ox, oy, sRaToast, t, charW, A); break;
        case 3: RaDrawBrilho(r, ox, oy, sRaToast, t, charW, A, now); break;
        case 4: RaDrawMedalha(r, ox, oy, sRaToast, t, charW, A); break;
        case 5: RaDrawVitral(r, ox, oy, sRaToast, t, charW, A); break;
        default: RaDrawCartao(r, ox, oy, sRaToast, t, charW, A); break;
    }
    return true;
}

extern "C" void Port_DebugMenu_Render(SDL_Renderer* renderer, int winW, int winH) {
    if (!renderer) {
        return;
    }

    const int charW = Port_DebugMenu_CharW(renderer);

    /* Rich RetroAchievements unlock overlay (issue #12), top-left. */
    RaRenderToast(renderer, winW, winH, charW);

    /* Toast: visible whether menu is open or not, e.g. after a warp. */
    if (!sToast.empty() && SDL_GetTicks() < sToastUntilTicks) {
        int textW = static_cast<int>(sToast.size()) * charW;
        float padX = charW * 0.75f;
        float padY = charW * 0.6f;
        float bgH = charW + padY * 2.0f;
        SDL_FRect bg = { (winW - textW) * 0.5f - padX, winH - bgH - 12.0f,
                         static_cast<float>(textW) + padX * 2.0f, bgH };
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 200);
        SDL_RenderFillRect(renderer, &bg);
        SDL_SetRenderDrawColor(renderer, 255, 240, 64, 255);
        SDL_RenderDebugText(renderer, bg.x + padX, bg.y + padY, sToast.c_str());
    }

    if (!sOpen || sPageStack.empty()) {
        return;
    }

    const MenuPage& page = sPageStack.back();

    /* Scroll viewport: clamp to a window of kVisibleItemsMax items. The
     * key handler keeps page.cursor inside [viewportTop, viewportTop + visible). */
    const int total = static_cast<int>(page.items.size());
    const int visible = std::min(total, kVisibleItemsMax);
    int top = page.viewportTop;
    if (top < 0) {
        top = 0;
    }
    if (top + visible > total) {
        top = std::max(0, total - visible);
    }
    const bool moreAbove = top > 0;
    const bool moreBelow = (top + visible) < total;

    /* Materialize each visible label up-front: cycle items reconstruct a
     * fresh string from labelFn() each frame, and we need the same value
     * for both column-width sizing and rendering below. */
    std::vector<std::string> visibleLabels;
    visibleLabels.reserve(static_cast<size_t>(visible));
    for (int i = top; i < top + visible && i < total; ++i) {
        const MenuItem& it = page.items[i];
        visibleLabels.push_back(it.labelFn ? it.labelFn() : it.label);
    }

    /* Reserve up to 4 extra rows for: title, "..." above, "..." below,
     * blank, and 2 hint lines at the bottom. */
    int rows = 2 + visible + (moreAbove ? 1 : 0) + (moreBelow ? 1 : 0) + 3;
    int cols = static_cast<int>(page.title.size());
    for (const auto& lbl : visibleLabels) {
        cols = std::max(cols, static_cast<int>(lbl.size()) + 4);
    }
    cols = std::max(cols, 36);

    /* All geometry derives from charW (which is already scaled on Switch), so
     * the box, padding and line pitch grow together with the font instead of
     * being fixed pixel counts that look cramped at 2x/3x. Row pitch leaves
     * ~28% leading between lines; inner padding is roughly one glyph cell. */
    const float kRowPitch = charW * 1.28f;
    const float padX = charW * 1.0f;
    const float padY = charW * 0.9f;
    float boxW = static_cast<float>(cols * charW) + padX * 2.0f;
    float boxH = static_cast<float>(rows) * kRowPitch + padY * 2.0f;
    SDL_FRect box = { (winW - boxW) * 0.5f, (winH - boxH) * 0.5f, boxW, boxH };

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 220);
    SDL_RenderFillRect(renderer, &box);
    SDL_SetRenderDrawColor(renderer, 200, 200, 200, 255);
    SDL_RenderRect(renderer, &box);

    const float textX = box.x + padX;
    float y = box.y + padY;

    SDL_SetRenderDrawColor(renderer, 200, 220, 255, 255);
    char titleBuf[160];
    if (total > kVisibleItemsMax) {
        std::snprintf(titleBuf, sizeof(titleBuf), "%s  [%d/%d]",
                      page.title.c_str(), page.cursor + 1, total);
    } else {
        std::snprintf(titleBuf, sizeof(titleBuf), "%s", page.title.c_str());
    }
    SDL_RenderDebugText(renderer, textX, y, titleBuf);
    y += kRowPitch + charW * 0.25f;

    if (moreAbove) {
        SDL_SetRenderDrawColor(renderer, 150, 150, 150, 255);
        SDL_RenderDebugText(renderer, textX, y, "  ^ ^ ^");
        y += kRowPitch;
    }

    for (int i = top; i < top + visible && i < total; ++i) {
        bool sel = i == page.cursor;
        const std::string& lbl = visibleLabels[static_cast<size_t>(i - top)];
        std::string line = (sel ? "> " : "  ") + lbl;
        if (sel) {
            SDL_SetRenderDrawColor(renderer, 255, 240, 64, 255);
        } else {
            SDL_SetRenderDrawColor(renderer, 230, 230, 230, 255);
        }
        SDL_RenderDebugText(renderer, textX, y, line.c_str());
        y += kRowPitch;
    }

    if (moreBelow) {
        SDL_SetRenderDrawColor(renderer, 150, 150, 150, 255);
        SDL_RenderDebugText(renderer, textX, y, "  v v v");
        y += kRowPitch;
    }

    y += charW * 0.25f;
    SDL_SetRenderDrawColor(renderer, 150, 150, 150, 255);
    SDL_RenderDebugText(renderer, textX, y,
        Tr("Up/Dn move  PgUp/PgDn page  Home/End ends",
           "Cima/Baixo mover  PgUp/PgDn pagina  Home/End extremos",
           "Arriba/Abajo mover  PgUp/PgDn pagina  Home/End extremos"));
    y += kRowPitch;
    SDL_RenderDebugText(renderer, textX, y,
        Tr("Enter select  L/R cycle  Esc back",
           "Enter seleciona  L/R altera  Esc volta",
           "Enter selecciona  L/R cambia  Esc atras"));
}
