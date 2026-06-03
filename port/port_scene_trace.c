/*
 * port_scene_trace.c — implementation of the [SCENE] trace tee (issue #28).
 *
 * Each [SCENE] line goes to TWO places:
 *   1. sdmc:/switch/tmc/tmc.log via OUR OWN FILE* (sLog), NOT stderr. This is
 *      the whole point of the rewrite: port_main.c:592 does
 *      `freopen("/dev/null", "w", stderr)` right before AgbMain() to silence the
 *      per-frame [disp]/[orch] spam — which also silenced every gameplay
 *      [SCENE] line (they fire inside AgbMain). Writing stderr here landed them
 *      in /dev/null, so tmc.log had 0 SCENE lines while nxlink had 90. By owning
 *      a dedicated append handle we survive that redirect.
 *   2. the nxlink socket fd, when launched via `nxlink -s`, for a live mirror.
 *
 * Plain POSIX only (fopen/fputs/write) — no <switch.h>, so this stays
 * compatible with the GBA game headers and builds on PC too. Compiled out on
 * TMC_RELEASE. cwd is sdmc:/switch/tmc (port_main chdir'd there at boot), so the
 * relative "tmc.log" lands next to the boot log.
 */
#include "port_scene_trace.h"

#if defined(PC_PORT) && !defined(TMC_RELEASE) && SCENE_TRACE

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* nxlink socket fd, or -1 when not launched via nxlink. */
static int sMirrorFd = -1;

/* Our own append handle to tmc.log, opened lazily on first trace line and kept
 * open. Append so it coexists with the boot log port_main already wrote, and
 * unbuffered so a crash still leaves the last line on disk. */
static FILE* sLog = NULL;

void Port_SceneTrace_SetMirrorFd(int fd) {
    sMirrorFd = fd;
}

void Port_SceneTrace_Emit(const char* fmt, ...) {
    char line[256];
    int n;
    va_list ap;

    /* "[SCENE] " + formatted body + newline, all in one buffer so the SD write
     * and the socket write carry an identical, atomic line. */
    memcpy(line, "[SCENE] ", 8);
    va_start(ap, fmt);
    n = vsnprintf(line + 8, sizeof(line) - 8 - 1, fmt, ap);
    va_end(ap);
    if (n < 0) {
        return;
    }
    n += 8; /* account for the prefix */
    if (n > (int)sizeof(line) - 2) {
        n = (int)sizeof(line) - 2; /* truncated; leave room for '\n' + '\0' */
    }
    line[n] = '\n';
    line[n + 1] = '\0';

    /* SD log via our own handle (NOT stderr — that's /dev/null during gameplay). */
    if (sLog == NULL) {
        sLog = fopen("tmc.log", "a");
        if (sLog != NULL) {
            setvbuf(sLog, NULL, _IONBF, 0); /* unbuffered: survives a freeze */
        }
    }
    if (sLog != NULL) {
        fputs(line, sLog);
    }

    /* Live mirror over nxlink, if active. Best-effort: ignore short/failed
     * writes so a dropped socket never disturbs the game. */
    if (sMirrorFd >= 0) {
        (void)write(sMirrorFd, line, (size_t)(n + 1));
    }
}

#endif /* PC_PORT && !TMC_RELEASE && SCENE_TRACE */
