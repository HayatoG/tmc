/*
 * switch_nxlink.c — optional live stdout/stderr over nxlink (debug aid).
 *
 * Kept in its own TU (like switch_net.c / switch_romfs.c) because <switch.h>
 * pulls in u8/u32 etc. that clash with the GBA game headers.
 *
 * When the .nro is launched with `nxlink -s`, the loader hands the app the
 * host PC's IP (libnx parses it into __nxlink_host) and keeps a socket open so
 * the app's stdout/stderr stream back to the `nxlink -s` terminal in real time.
 * The normal path (launched from hbmenu/sphaira) leaves __nxlink_host zeroed,
 * so this is a no-op there.
 *
 * port_main.c redirects stderr to sdmc:/switch/tmc/tmc.log so all the
 * fprintf(stderr,...) tracing (incl. the issue #28 [SCENE] lines) lands on the
 * SD. To ALSO see it live over nxlink we:
 *   1. detect whether we were launched via nxlink (Port_Switch_NxlinkActive),
 *      so port_main can decide whether to also tee stderr to the socket;
 *   2. expose Port_Switch_NxlinkStdio() which calls libnx's nxlinkStdio() to
 *      point the standard streams at the host.
 *
 * Networking must be up first (socketInitializeDefault, done by Port_Net_Init
 * in switch_net.c) — nxlinkStdio() just opens a socket to __nxlink_host.
 */
#include <switch.h>

#include <netinet/in.h> /* struct in_addr — __nxlink_host's type */
#include <stdio.h>

/* True iff the app was launched via `nxlink` (host IP present). Safe to call
 * before sockets are initialized — it only inspects the parsed host address. */
int Port_Switch_NxlinkActive(void) {
    return __nxlink_host.s_addr != 0;
}

/* Open the nxlink host socket so the trace shows up in `nxlink -s`, and return
 * its fd (or -1 if not launched via nxlink / on failure).
 *
 * We deliberately redirect ONLY stdout (nxlinkConnectToHost(true, false)), NOT
 * stderr: port_main already freopen()'d stderr onto sdmc:/switch/tmc/tmc.log
 * and nxlinkStdio()'s stderr redirect would clobber that, losing the SD log.
 * The returned fd is handed to the trace layer, which write()s each [SCENE]
 * line to it in addition to stderr — so both destinations get the line.
 *
 * Requires socketInitializeDefault() to have run already (Port_Net_Init). */
int Port_Switch_NxlinkStdio(void) {
    if (__nxlink_host.s_addr == 0) {
        return -1;
    }
    return nxlinkConnectToHost(true, false);
}
