/*
 * Switch docked/handheld resolution handling.
 *
 * Kept in its own TU because <switch.h> defines u8/u16/u32 etc. that clash
 * with the GBA game headers used elsewhere (same reason as switch_romfs.c).
 *
 * The port has no main loop of its own — the loop is the game's AgbMain() —
 * so this is driven once per presented frame from Port_PPU_PresentFrame().
 *
 * IMPORTANT: appletGetOperationMode() only refreshes its cached state when the
 * applet message loop is pumped (appletMainLoop() internally does
 * appletGetMessage + appletProcessMessage). Polling it without pumping returns
 * stale state and never sees a dock/undock — so the pump below is mandatory.
 * appletMainLoop() also processes the OS sleep/HOME messages, so leaving the
 * default focus-handling mode in place lets the system suspend/resume cleanly.
 *
 * Logging goes to its OWN file (sdmc:/switch/tmc/applet.log), NOT stderr:
 * port_main.c does freopen("/dev/null", stderr) right before AgbMain to kill
 * per-frame spam, so anything we'd fprintf(stderr) from here (inside the frame
 * loop) would be discarded. A dedicated unbuffered file survives that.
 */
#include <switch.h>

#include <stdio.h>
#include <stdarg.h>

/* cwd is sdmc:/switch/tmc (port_main chdir'd there before the game starts). */
static FILE* sLog = NULL;

static void alog(const char* fmt, ...) {
    if (sLog == NULL) {
        sLog = fopen("applet.log", "w");
        if (sLog == NULL) {
            return;
        }
        setvbuf(sLog, NULL, _IONBF, 0); /* unbuffered: survives a freeze */
    }
    va_list ap;
    va_start(ap, fmt);
    vfprintf(sLog, fmt, ap);
    va_end(ap);
}

/*
 * Once-per-frame tick. Pumps the applet message loop, then reports a
 * docked/handheld resolution change via outW, outH and resized
 * (handheld = 1280x720, docked/console = 1920x1080). The first call always
 * reports a "change" (sentinel -1) so the window is sized to the current
 * operation mode at startup.
 */
void Port_Switch_AppletTick(int* outW, int* outH, int* resized) {
    static AppletOperationMode sLastMode = (AppletOperationMode)-1;

    appletMainLoop(); /* pump messages so appletGetOperationMode() refreshes */

    *resized = 0;
    AppletOperationMode mode = appletGetOperationMode();
    if (mode != sLastMode) {
        int docked = (mode == AppletOperationMode_Console);
        int w = docked ? 1920 : 1280;
        int h = docked ? 1080 : 720;
        alog("[applet] operation mode %d -> %d (%dx%d)\n",
             (int)sLastMode, (int)mode, w, h);
        sLastMode = mode;
        *outW = w;
        *outH = h;
        *resized = 1;
    }
}
