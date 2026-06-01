/*
 * SDL3 -> SDL2 compatibility shim for the Switch build.
 *
 * The Minish Cap PC port targets SDL3, but devkitPro only ships switch-sdl2.
 * This header maps the subset of the SDL3 API the port actually uses onto
 * SDL2 equivalents. It is force-included via <SDL3/SDL.h> -> SDL3/SDL.h
 * (in platforms/switch/compat) which includes real SDL2 then this file.
 *
 * Scope: everything that maps cleanly by macro/inline wrapper. Things that
 * cannot (audio push/pull model, SDL_Event union field names, gamepad
 * instance-ID enumeration) are handled with `#ifdef __SWITCH__` blocks at
 * the call sites (port_audio.c, port_bios.c, port_runtime_config.cpp).
 */
#ifndef TMC_SDL3_TO_SDL2_H
#define TMC_SDL3_TO_SDL2_H
#ifdef __SWITCH__

#include <stdbool.h>
#include <stdint.h>

/* ---- Init / subsystem: SDL3 returns bool(true=ok); SDL2 int(0=ok). ----
 * The self-reference rule means the inner token is not re-expanded, so it
 * calls the real SDL2 function. */
#define SDL_Init(flags)            (SDL_Init(flags) == 0)
#define SDL_InitSubSystem(flags)   (SDL_InitSubSystem(flags) == 0)

/* ---- Hints (string-valued; harmless/ignored on the single Switch driver) */
#ifndef SDL_HINT_VIDEO_DRIVER
#define SDL_HINT_VIDEO_DRIVER "SDL_VIDEODRIVER"
#endif
#ifndef SDL_HINT_AUDIO_DRIVER
#define SDL_HINT_AUDIO_DRIVER "SDL_AUDIODRIVER"
#endif
/* SDL_HINT_RENDER_DRIVER exists in SDL2 already. */

/* ---- Event type enum renames ---- */
#define SDL_EVENT_QUIT                SDL_QUIT
#define SDL_EVENT_KEY_DOWN            SDL_KEYDOWN
#define SDL_EVENT_KEY_UP              SDL_KEYUP
#define SDL_EVENT_GAMEPAD_ADDED       SDL_CONTROLLERDEVICEADDED
#define SDL_EVENT_GAMEPAD_REMOVED     SDL_CONTROLLERDEVICEREMOVED
#define SDL_EVENT_GAMEPAD_BUTTON_DOWN SDL_CONTROLLERBUTTONDOWN
#define SDL_EVENT_GAMEPAD_AXIS_MOTION SDL_CONTROLLERAXISMOTION
#define SDL_EVENT_JOYSTICK_ADDED      SDL_JOYDEVICEADDED
#define SDL_EVENT_JOYSTICK_REMOVED    SDL_JOYDEVICEREMOVED

/* ---- Keyboard modifier rename ---- */
#define SDL_KMOD_ALT  KMOD_ALT

/* ---- Gamepad: SDL3 "Gamepad" == SDL2 "GameController" ---- */
typedef SDL_GameController       SDL_Gamepad;
typedef SDL_GameControllerAxis   SDL_GamepadAxis;
typedef SDL_GameControllerButton SDL_GamepadButton;

#define SDL_OpenGamepad        SDL_GameControllerOpen
#define SDL_CloseGamepad       SDL_GameControllerClose
#define SDL_GetGamepadAxis     SDL_GameControllerGetAxis
#define SDL_GetGamepadButton   SDL_GameControllerGetButton
#define SDL_IsGamepad          SDL_IsGameController
#define SDL_GetGamepadName     SDL_GameControllerName
#define SDL_UpdateGamepads     SDL_GameControllerUpdate

#define SDL_GAMEPAD_BUTTON_INVALID      SDL_CONTROLLER_BUTTON_INVALID
#define SDL_GAMEPAD_BUTTON_WEST         SDL_CONTROLLER_BUTTON_X
#define SDL_GAMEPAD_BUTTON_COUNT        SDL_CONTROLLER_BUTTON_MAX
#define SDL_GAMEPAD_AXIS_INVALID        SDL_CONTROLLER_AXIS_INVALID
#define SDL_GAMEPAD_AXIS_LEFTX          SDL_CONTROLLER_AXIS_LEFTX
#define SDL_GAMEPAD_AXIS_LEFTY          SDL_CONTROLLER_AXIS_LEFTY
#define SDL_GAMEPAD_AXIS_LEFT_TRIGGER   SDL_CONTROLLER_AXIS_TRIGGERLEFT
#define SDL_GAMEPAD_AXIS_COUNT          SDL_CONTROLLER_AXIS_MAX

static inline SDL_JoystickID sdl3compat_GetGamepadID(SDL_GameController* gc) {
    return SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(gc));
}
#define SDL_GetGamepadID sdl3compat_GetGamepadID

/* ---- Init flag + misc enum renames ---- */
#define SDL_INIT_GAMEPAD SDL_INIT_GAMECONTROLLER

/* SDL3 SDL_GetScancodeFromKey(key, SDL_Keymod*) -> SDL2 takes key only. */
#define SDL_GetScancodeFromKey(k, m) (SDL_GetScancodeFromKey(k))

/* ---- Renderer ---- */
/* SDL3 SDL_CreateRenderer(window, const char* name) -> SDL2 (window, index, flags). */
#define SDL_CreateRenderer(win, name) (SDL_CreateRenderer((win), -1, 0))
#define SDL_SetRenderVSync            SDL_RenderSetVSync
#define SDL_GetCurrentRenderOutputSize SDL_GetRendererOutputSize
#define SDL_RenderFillRect           SDL_RenderFillRectF  /* SDL3 takes SDL_FRect */
#define SDL_RenderRect               SDL_RenderDrawRectF

/* SDL3 SDL_RenderTexture(r, t, const SDL_FRect* src, const SDL_FRect* dst).
 * SDL2 SDL_RenderCopyF wants an SDL_Rect* src, so convert when non-NULL. */
static inline bool sdl3compat_RenderTexture(SDL_Renderer* r, SDL_Texture* t,
                                            const SDL_FRect* src, const SDL_FRect* dst) {
    if (src) {
        SDL_Rect s = { (int)src->x, (int)src->y, (int)src->w, (int)src->h };
        return SDL_RenderCopyF(r, t, &s, dst) == 0;
    }
    return SDL_RenderCopyF(r, t, NULL, dst) == 0;
}
#define SDL_RenderTexture sdl3compat_RenderTexture

/* ---- Scale mode (exists in SDL2, different spelling) ---- */
#define SDL_SCALEMODE_NEAREST SDL_ScaleModeNearest
#define SDL_SCALEMODE_LINEAR  SDL_ScaleModeLinear

/* ---- Window + renderer creation (SDL3 adds a leading title arg) ---- */
static inline bool sdl3compat_CreateWindowAndRenderer(const char* title, int w, int h,
                                                      Uint32 flags,
                                                      SDL_Window** win, SDL_Renderer** ren) {
    if (SDL_CreateWindowAndRenderer(w, h, flags, win, ren) != 0)
        return false;
    if (win && *win && title)
        SDL_SetWindowTitle(*win, title);
    return true;
}
#define SDL_CreateWindowAndRenderer sdl3compat_CreateWindowAndRenderer

/* ---- Time: SDL3 has nanosecond ticks; SDL2 only ms. ---- */
static inline uint64_t sdl3compat_GetTicksNS(void) {
    return (uint64_t)SDL_GetPerformanceCounter() * 1000000000ULL
           / SDL_GetPerformanceFrequency();
}
#define SDL_GetTicksNS sdl3compat_GetTicksNS

/* ---- Surface API renames ---- */
#define SDL_DestroySurface  SDL_FreeSurface
#define SDL_FillSurfaceRect SDL_FillRect

static inline SDL_Surface* sdl3compat_CreateSurfaceFrom(int w, int h, Uint32 format,
                                                        void* pixels, int pitch) {
    int bpp; Uint32 rm, gm, bm, am;
    SDL_PixelFormatEnumToMasks(format, &bpp, &rm, &gm, &bm, &am);
    return SDL_CreateRGBSurfaceWithFormatFrom(pixels, w, h, bpp, pitch, format);
}
#define SDL_CreateSurfaceFrom sdl3compat_CreateSurfaceFrom

static inline bool sdl3compat_BlitSurfaceScaled(SDL_Surface* src, const SDL_Rect* srcrect,
                                                SDL_Surface* dst, SDL_Rect* dstrect,
                                                int scaleMode) {
    (void)scaleMode;
    return SDL_BlitScaled(src, srcrect, dst, dstrect) == 0;
}
#define SDL_BlitSurfaceScaled sdl3compat_BlitSurfaceScaled

/* ---- SDL3-only calls with no SDL2 counterpart: stub harmlessly ---- */
static inline bool sdl3compat_SyncWindow(SDL_Window* w) { (void)w; return true; }
#define SDL_SyncWindow sdl3compat_SyncWindow

static inline bool sdl3compat_DestroyWindowSurface(SDL_Window* w) { (void)w; return true; }
#define SDL_DestroyWindowSurface sdl3compat_DestroyWindowSurface

static inline bool sdl3compat_SetWindowSurfaceVSync(SDL_Window* w, int v) {
    (void)w; (void)v; return true;
}
#define SDL_SetWindowSurfaceVSync sdl3compat_SetWindowSurfaceVSync

/* Debug text overlay: switch-sdl2 has no SDL_RenderDebugText, so we provide a
 * real 8x8 bitmap-font renderer (compat/sdl3_debug_text.c) that draws in the
 * renderer's current draw colour via SDL_RenderFillRect. Used by the in-game
 * settings overlay + FPS counter. */
#ifndef SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE
#define SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE 8
#endif
#ifdef __cplusplus
extern "C" {
#endif
bool sdl3compat_RenderDebugText(SDL_Renderer* r, float x, float y, const char* str);
/* Integer scale the overlay font is drawn at (derived from output height). */
int sdl3compat_DebugTextScale(SDL_Renderer* r);
/* Draw debug text at an extra integer multiplier over the base scale. */
bool sdl3compat_RenderDebugTextScaled(SDL_Renderer* r, float x, float y,
                                      const char* str, int extraScale);
#ifdef __cplusplus
}
#endif
#define SDL_RenderDebugText sdl3compat_RenderDebugText

#endif /* __SWITCH__ */
#endif /* TMC_SDL3_TO_SDL2_H */
