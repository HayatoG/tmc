/*
 * switch_net.c — minimal HTTPS networking for the Switch port (issue #12 infra).
 *
 * Kept in its own TU (like switch_romfs.c / switch_applet.c) because <switch.h>
 * and <curl/curl.h> pull in u8/u32 etc. that clash with the GBA game headers.
 *
 * libcurl here is the devkitPro switch-curl portlib built against the libnx TLS
 * backend (the system `ssl` service), so HTTPS validates against the console's
 * own CA store automatically — no bundled cacert.pem. DNS/proxy come from the
 * socket driver. We only need socketInitializeDefault() at boot.
 *
 * This first cut exposes:
 *   - Port_Net_Init / Port_Net_Exit  — socket lifecycle (called from port_main)
 *   - Port_Net_HttpGet               — blocking GET, body into a caller buffer
 *   - Port_Net_SmokeTest             — one HTTPS GET, result logged to net.log,
 *                                      so the infra can be validated on hardware
 *                                      before any RetroAchievements code exists.
 *
 * RELEASE builds skip the SD log (same convention as the other switch_*.c).
 */
#include <switch.h>

#include <curl/curl.h>

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

/* cwd is sdmc:/switch/tmc (port_main chdir'd there before the game starts). */
#ifndef TMC_RELEASE
static FILE* sLog = NULL;

static void nlog(const char* fmt, ...) {
    if (sLog == NULL) {
        sLog = fopen("net.log", "w");
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
#else
static void nlog(const char* fmt, ...) { (void)fmt; }
#endif

static bool sNetReady = false;

/* Bring up the socket driver. Safe to call once at boot. Library-applet launches
 * are already rejected earlier (issue #17), so the default socket config (which
 * reserves ~1-2 MiB of transfer memory) is fine — we only ever run in
 * Application mode with full memory here. */
void Port_Net_Init(void) {
    Result rc = socketInitializeDefault();
    if (R_FAILED(rc)) {
        nlog("[net] socketInitializeDefault failed: 0x%x\n", rc);
        sNetReady = false;
        return;
    }
    curl_global_init(CURL_GLOBAL_DEFAULT);
    sNetReady = true;
    nlog("[net] socket + curl initialized\n");
}

void Port_Net_Exit(void) {
    if (!sNetReady) {
        return;
    }
    curl_global_cleanup();
    socketExit();
    sNetReady = false;
}

/* curl write callback: append received bytes into a fixed caller buffer,
 * truncating (never overflowing) if the response exceeds capacity. */
typedef struct {
    char*  buf;
    size_t cap;  /* total capacity incl. space for the NUL */
    size_t len;  /* bytes written so far (excl. NUL) */
} WriteCtx;

static size_t write_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    size_t incoming = size * nmemb;
    WriteCtx* w = (WriteCtx*)userdata;
    size_t room = (w->cap > w->len + 1) ? (w->cap - w->len - 1) : 0;
    size_t take = incoming < room ? incoming : room;
    if (take > 0) {
        memcpy(w->buf + w->len, ptr, take);
        w->len += take;
        w->buf[w->len] = '\0';
    }
    /* Always claim the full amount so curl doesn't abort on truncation. */
    return incoming;
}

/*
 * Blocking HTTPS GET. Writes the (NUL-terminated, possibly truncated) response
 * body into out_body[0..out_cap-1]. Returns the HTTP status code (e.g. 200), or
 * a negative value on transport failure. out_len (optional) receives the body
 * length. This is the primitive the rc_client server-call callback will sit on.
 */
long Port_Net_HttpGet(const char* url, char* out_body, size_t out_cap, size_t* out_len) {
    if (out_cap > 0) {
        out_body[0] = '\0';
    }
    if (out_len) {
        *out_len = 0;
    }
    if (!sNetReady) {
        nlog("[net] HttpGet called before init\n");
        return -1;
    }
    CURL* curl = curl_easy_init();
    if (!curl) {
        return -2;
    }
    WriteCtx ctx = { out_body, out_cap, 0 };
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "TMC-Switch/0.2 (libnx curl)");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);

    CURLcode res = curl_easy_perform(curl);
    long status = -3;
    if (res == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        if (out_len) {
            *out_len = ctx.len;
        }
    } else {
        nlog("[net] curl_easy_perform failed: %s\n", curl_easy_strerror(res));
    }
    curl_easy_cleanup(curl);
    return status;
}

/*
 * One-shot HTTPS smoke test, logged to sdmc:/switch/tmc/net.log. Proves the
 * whole path (socket init → DNS → TLS via system CA store → HTTP) end to end on
 * real hardware, independent of any RetroAchievements code. Hits the RA host so
 * we also confirm reachability of the service we actually care about.
 */
void Port_Net_SmokeTest(void) {
    static char body[1024];
    size_t len = 0;
    const char* url = "https://retroachievements.org/API/API_GetTopTenUsers.php";
    long status = Port_Net_HttpGet(url, body, sizeof body, &len);
    nlog("[net] smoke GET %s -> status=%ld, %zu bytes\n", url, status, len);
    if (len > 0) {
        size_t preview = len < 200 ? len : 200;
        nlog("[net] body[0..%zu]: %.*s\n", preview, (int)preview, body);
    }
}
