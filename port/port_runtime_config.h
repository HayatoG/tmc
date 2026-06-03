#pragma once

#include "port_types.h"
#include <SDL3/SDL.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PORT_INPUT_A,
    PORT_INPUT_B,
    PORT_INPUT_SELECT,
    PORT_INPUT_START,
    PORT_INPUT_RIGHT,
    PORT_INPUT_LEFT,
    PORT_INPUT_UP,
    PORT_INPUT_DOWN,
    PORT_INPUT_R,
    PORT_INPUT_L,
    /* Extra equip buttons (port_softslots.c). Each maps to a configurable
     * key/pad/trigger and is read every frame to decide which item — if any —
     * fires through the B-dispatch path. */
    PORT_INPUT_SOFT_X,
    PORT_INPUT_SOFT_Y,
    PORT_INPUT_SOFT_L2,
    PORT_INPUT_SOFT_R2,
    PORT_INPUT_COUNT,
} PortInput;

void Port_Config_Load(const char* path);
u8 Port_Config_WindowScale(void);
const char* Port_Config_UpscaleMethod(void);
u64 Port_Config_FrameTimeNs(void);
u32 Port_Config_TargetFps(void);
bool Port_Config_PortSettingsMenuEnabled(void);
bool Port_Config_ShowFps(void);
void Port_Config_ToggleShowFps(void);

/* FPS counter placement (4 corners) and size, persisted in config.json.
 * Corner: 0=top-left 1=top-right 2=bottom-left 3=bottom-right.
 * Scale: 1..4 extra multiplier on top of the resolution-derived font scale. */
int Port_Config_FpsCorner(void);
void Port_Config_CycleFpsCorner(int direction);
int Port_Config_FpsScale(void);
void Port_Config_CycleFpsScale(int direction);
/* Dark semi-transparent panel behind the FPS counter (legibility). */
bool Port_Config_FpsBackground(void);
void Port_Config_ToggleFpsBackground(void);

/* RetroAchievements unlock-toast visual style (issue #12). One of 6 variants:
 * 0=Pilula 1=Cartao 2=Minimo 3=Brilho 4=Medalha 5=Vitral. Persisted. */
int Port_Config_RaOverlayVariant(void);
void Port_Config_CycleRaOverlayVariant(int direction);

/* Overlay UI language: 0 = English, 1 = Português. Defaults to the console
 * language on Switch, persisted in config.json. */
int Port_Config_Language(void);
void Port_Config_CycleLanguage(int direction);
void Port_Config_SetWindowScale(u8 scale);
void Port_Config_SetUpscaleMethod(const char* method);
void Port_Config_SetTargetFps(u32 fps);
void Port_Config_CycleTargetFps(int direction);

/* Internal render-resolution multiplier. The PPU normally renders at the
 * GBA-native 240x160; with scale=N>1 it produces a 240*N by 160*N
 * framebuffer, with sub-pixel sampling on affine paths (OAM affine,
 * mode2 BG2, mode7) so rotated layers and scaled sprites stop staircase-
 * aliasing. Text BGs and non-affine sprites are simply S*S nearest-
 * replicated — pixel-art has no information to recover at higher density.
 * Range 1..4 (capped by PPU framebuffer height of 640 = 160*4). */
u8 Port_Config_InternalScale(void);
void Port_Config_SetInternalScale(u8 scale);
void Port_Config_CycleInternalScale(int direction);
void Port_Config_OpenGamepads(void);
void Port_Config_HandleEvent(const SDL_Event* e);
bool Port_Config_InputPressed(PortInput input);
void Port_Config_CloseGamepads(void);

/* Soft-slot input poll, indexed 0..3 (X, Y, L2, R2). */
bool Port_Config_SoftSlotPressed(int slot);

/* Left-analog-stick direction bitmask. Returns any combination of the
 * PORT_DPAD_* flags below for the directions the stick is pushed past its
 * dead zone (diagonals set two flags). The port ORs this with the physical
 * D-pad so the stick moves Link without overriding the D-pad. */
enum {
    PORT_DPAD_RIGHT = 1 << 0,
    PORT_DPAD_LEFT  = 1 << 1,
    PORT_DPAD_UP    = 1 << 2,
    PORT_DPAD_DOWN  = 1 << 3,
};
int Port_Config_AnalogDPad(void);

/* Clear the per-input "pressed this frame" edge cache. Call after the
 * port has committed KEYINPUT and the engine has read it, so the next
 * frame's polled state isn't stuck reporting the previous tap. */
void Port_Config_ClearInputEdges(void);

#ifdef __cplusplus
}
#endif
