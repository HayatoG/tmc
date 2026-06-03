#ifndef PORT_SCENE_TRACE_H
#define PORT_SCENE_TRACE_H

/*
 * Scene/room transition tracing (issue #28).
 *
 * The opening cutscene — handing the sword to Zelda at Hyrule Castle — warps to
 * "North Hyrule Field" but the room comes up EMPTY: no entities spawn, not even
 * the Zelda follower, so the player is stuck. To debug it we log every step of
 * the room/area/cutscene transition pipeline.
 *
 * Output goes through stderr, which port_main.c freopen()s to
 *   sdmc:/switch/tmc/tmc.log  (unbuffered, APPEND so a crash+relaunch doesn't
 * wipe it). This is NOT just a boot log — stderr stays redirected for the whole
 * session, so these runtime lines land in the same file.
 *
 * When launched via `nxlink -s`, port_main also hands us the nxlink socket fd
 * (Port_SceneTrace_SetMirrorFd) so each [SCENE] line is ALSO written there and
 * shows up live in the host terminal — i.e. tmc.log AND nxlink at once.
 *
 * Compiled out entirely on TMC_RELEASE (the SD log is /dev/null'd there anyway)
 * and off the PC_PORT path, so it costs nothing in shipping builds.
 */

#if defined(PC_PORT) && !defined(TMC_RELEASE)

#include <stdarg.h>
#include <stdio.h>

/* Toggle at build time with -DSCENE_TRACE=0 to silence without removing the
 * call sites. On by default so a debug build just works. */
#ifndef SCENE_TRACE
#define SCENE_TRACE 1
#endif

#if SCENE_TRACE

#ifdef __cplusplus
extern "C" {
#endif
/* Set the extra fd (nxlink socket) that trace lines are mirrored to, or -1 to
 * disable. Called once from port_main after nxlinkStdio(). */
void Port_SceneTrace_SetMirrorFd(int fd);
/* printf-style; prefixes "[SCENE] ", appends "\n", writes to stderr (tmc.log)
 * and, if set, the mirror fd. Defined in port_scene_trace.c. */
void Port_SceneTrace_Emit(const char* fmt, ...);
#ifdef __cplusplus
}
#endif

#define SCENE_LOG(...) Port_SceneTrace_Emit(__VA_ARGS__)

#else
#define SCENE_LOG(...) ((void)0)
#endif

#else /* shipping / non-PC build */
#define SCENE_LOG(...) ((void)0)
#endif

#endif /* PORT_SCENE_TRACE_H */
