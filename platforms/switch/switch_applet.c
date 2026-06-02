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
#ifdef TMC_RELEASE
    (void)fmt; /* release build: no SD logging */
#else
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
#endif
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

/* Console system language → overlay UI language code: 0 = English,
 * 1 = Português, 2 = Español. Used as the default on first run (the user can
 * still override it in the settings overlay). Returns 0 on any error so English
 * is the safe fallback. */
int Port_Switch_SystemLanguage(void) {
    int result = 0; /* English */
    if (R_SUCCEEDED(setInitialize())) {
        u64 langCode = 0;
        SetLanguage lang = SetLanguage_ENUS;
        if (R_SUCCEEDED(setGetSystemLanguage(&langCode)) &&
            R_SUCCEEDED(setMakeLanguage(langCode, &lang))) {
            /* SetLanguage_PT = European Portuguese; SetLanguage_PTBR =
             * Brazilian Portuguese [10.1.0+]. Match both. */
            if (lang == SetLanguage_PT || lang == SetLanguage_PTBR) {
                result = 1; /* Português */
            } else if (lang == SetLanguage_ES) {
                result = 2; /* Español */
            }
        }
        setExit();
    }
    return result;
}

/* True when the port was launched as a library applet (e.g. opened through the
 * Homebrew Menu's Album entry) instead of a full application. Library applets
 * get a small memory pool, which is not enough for the port (16 MB ROM + ~14 MB
 * map data + heaps), so it fails to start. We detect this up front and warn the
 * user (port_main.c) instead of crashing silently. AppletType_Application (0)
 * and AppletType_SystemApplication (4) have the full pool; only
 * AppletType_LibraryApplet (2) is the limited case we reject. */
int Port_Switch_IsLibraryApplet(void) {
    return appletGetAppletType() == AppletType_LibraryApplet;
}

/* Show a blocking, native Switch error dialog (the system error applet) and
 * wait for the user to dismiss it. SDL_ShowSimpleMessageBox does NOT work on
 * the Switch — there is no host window manager to draw it, so it returns
 * immediately and nothing is shown. The error applet is the correct modal:
 * always visible, dismissable with a button. `short_msg` is the dialog title
 * line, `detail` the body. Both are plain ASCII (the applet renders system
 * fonts, so accents are fine too, but we keep ASCII for consistency). */
void Port_Switch_ShowFatalMessage(const char* short_msg, const char* detail) {
    ErrorSystemConfig c;
    if (R_SUCCEEDED(errorSystemCreate(&c, short_msg, detail))) {
        errorSystemShow(&c); /* blocks until the user closes the dialog */
    }
}
