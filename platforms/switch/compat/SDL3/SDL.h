/*
 * Switch build shim: the PC port includes <SDL3/SDL.h> everywhere, but the
 * Switch only has switch-sdl2. This header lives FIRST on the include path
 * (platforms/switch/compat) so every `#include <SDL3/SDL.h>` resolves here,
 * pulls in real SDL2, and then layers the SDL3→SDL2 API shim on top.
 *
 * Only compiled into the Switch target (guarded by __SWITCH__). On PC the
 * real SDL3 headers are used.
 */
#ifndef TMC_SWITCH_SDL3_REDIRECT_H
#define TMC_SWITCH_SDL3_REDIRECT_H

#include <SDL.h>          /* real switch-sdl2 */
#include "sdl3_to_sdl2.h" /* the API compatibility shim */

#endif
