/*
 * port_retroachievements.c — RetroAchievements integration via rcheevos
 * (rc_client). Issue #12.
 *
 * rc_client is the high-level RetroAchievements client: it manages login, game
 * identification, and per-frame achievement evaluation. It depends on two
 * callbacks the integrator supplies:
 *
 *   1. server_call  — perform an HTTP(S) request. We route it to the Switch
 *                     networking helpers (switch_net.c, libnx curl + system TLS).
 *   2. read_memory  — read the emulated console RAM. We map the RA address space
 *                     (see the GBA memory regions in rcheevos consoleinfo.c) onto
 *                     our emulated gIwram/gEwram (port_gba_mem.c).
 *
 * This TU is plain C and does NOT include <switch.h> (it would clash with the
 * GBA game headers); the Switch-specific bits are reached through the
 * Port_Net_* / Port_RA_Swkbd_* externs implemented in the switch_*.c TUs.
 *
 * Policy (issue #12, first cut): softcore only — achievements unlock but save
 * states stay enabled (no hardcore lockout). PC build keeps RA off for now;
 * this is wired on the Switch where networking lives.
 */
#include "rc_client.h"
#include "rc_api_request.h"
#include "rc_hash.h"
#include "rc_consoles.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

/* Emulated GBA work RAM (port_gba_mem.c). */
extern unsigned char gEwram[0x40000]; /* 256 KB External Work RAM @ 0x02000000 */
extern unsigned char gIwram[0x8000];  /* 32 KB  Internal Work RAM @ 0x03000000 */

/* The loaded cartridge ROM (port_rom.c) — 16 MB, used to compute the RA hash. */
extern unsigned char* gRomData;
extern unsigned int   gRomSize;

/* HTTP helpers (switch_net.c). Negative status = transport failure.
 * Port_Net_HttpRequest grows the body buffer dynamically (RA PatchData is large)
 * and returns a malloc'd body the caller must free(). */
extern long Port_Net_HttpRequest(const char* url, const char* post_data, const char* content_type,
                                 char** out_body, size_t* out_len);

/* Native software keyboard (switch_net.c). Returns 1 on OK, 0 on cancel. */
extern int Port_Swkbd_Get(const char* header, int password, char* out, size_t out_cap);

/*
 * The RA login token is persisted in its own small file (sdmc:/switch/tmc/
 * ra_token, "username\ntoken\n") rather than config.json — it's a credential,
 * kept out of the general settings file and easy to wipe. We store the TOKEN,
 * never the password (RA issues the token at login; it's what re-login uses).
 */
#define RA_TOKEN_PATH "ra_token"

static int save_token(const char* username, const char* token) {
    FILE* f = fopen(RA_TOKEN_PATH, "w");
    if (!f) return -1;
    fprintf(f, "%s\n%s\n", username, token);
    fclose(f);
    return 0;
}

/* Load saved username+token. Returns 1 if both present. */
static int load_token(char* user, size_t user_cap, char* token, size_t token_cap) {
    FILE* f = fopen(RA_TOKEN_PATH, "r");
    if (!f) return 0;
    int ok = 0;
    if (fgets(user, (int)user_cap, f) && fgets(token, (int)token_cap, f)) {
        user[strcspn(user, "\r\n")] = '\0';
        token[strcspn(token, "\r\n")] = '\0';
        ok = (user[0] && token[0]);
    }
    fclose(f);
    return ok;
}

static rc_client_t* sClient = NULL;

/* ----- logging (own file, like the other Switch TUs) -------------------- */
#ifndef TMC_RELEASE
static FILE* sLog = NULL;
static void ralog(const char* fmt, ...) {
    if (sLog == NULL) {
        sLog = fopen("ra.log", "w");
        if (!sLog) return;
        setvbuf(sLog, NULL, _IONBF, 0);
    }
    va_list ap;
    va_start(ap, fmt);
    vfprintf(sLog, fmt, ap);
    va_end(ap);
}
#else
static void ralog(const char* fmt, ...) { (void)fmt; }
#endif

/* ----- memory peek ------------------------------------------------------ */
/*
 * RA presents the GBA as a flat address space (rcheevos consoleinfo.c,
 * _rc_memory_regions_gameboy_advance):
 *   0x000000..0x007FFF -> IWRAM  (32 KB,  console 0x03000000)
 *   0x008000..0x047FFF -> EWRAM  (256 KB, console 0x02000000)
 *   0x048000..0x057FFF -> Save RAM (64 KB) — the TMC port has no mapped SRAM
 *                         (saves go through EEPROM/quicksave), so reads here
 *                         return 0. Minish Cap achievements live in work RAM.
 * Returns the number of bytes actually read (rc_client treats a short read as
 * the region boundary).
 */
static uint32_t read_memory(uint32_t address, uint8_t* buffer, uint32_t num_bytes, rc_client_t* client) {
    (void)client;
    uint32_t read = 0;
    while (read < num_bytes) {
        uint32_t a = address + read;
        uint8_t byte;
        if (a < 0x008000u) {
            byte = gIwram[a];
        } else if (a < 0x048000u) {
            byte = gEwram[a - 0x008000u];
        } else {
            break; /* Save RAM / out of range — stop here. */
        }
        buffer[read++] = byte;
    }
    return read;
}

/* ----- server call (HTTP) ----------------------------------------------- */
/* rc_client hands us a request (url, optional post_data) and a callback to feed
 * the response back into. We perform it synchronously via curl and invoke the
 * callback. Response bodies are small (JSON), so a fixed buffer is fine. */
static void server_call(const rc_api_request_t* request,
                        rc_client_server_callback_t callback,
                        void* callback_data,
                        rc_client_t* client) {
    (void)client;
    char*  body = NULL;
    size_t len = 0;
    /* GET when there's no post_data; the dynamic buffer handles large PatchData. */
    const char* post = (request->post_data && request->post_data[0]) ? request->post_data : NULL;
    long status = Port_Net_HttpRequest(request->url, post, request->content_type, &body, &len);

    rc_api_server_response_t response;
    memset(&response, 0, sizeof response);
    response.body = body ? body : "";
    response.body_length = len;
    response.http_status_code = (int)status; /* negative on transport failure */

    if (status < 0) {
        ralog("[ra] server_call transport error (%ld) for %s\n", status, request->url);
    }
    callback(&response, callback_data);
    free(body);
}

/* ----- events (unlock toasts) ------------------------------------------- */
/* A C-linkage toast helper implemented over the overlay's Toast() (debug menu).
 * Shows a short on-screen message — used to announce achievement unlocks. */
extern void Port_RA_Toast(const char* msg);

static void event_handler(const rc_client_event_t* event, rc_client_t* client) {
    (void)client;
    switch (event->type) {
        case RC_CLIENT_EVENT_ACHIEVEMENT_TRIGGERED:
            if (event->achievement && event->achievement->title) {
                char msg[160];
                snprintf(msg, sizeof msg, "Achievement: %s", event->achievement->title);
                Port_RA_Toast(msg);
                ralog("[ra] unlocked: %s\n", event->achievement->title);
            }
            break;
        case RC_CLIENT_EVENT_GAME_COMPLETED:
            Port_RA_Toast("All achievements unlocked!");
            break;
        default:
            break;
    }
}

/* ----- debug: simulate an unlock (issue #25) ---------------------------- */
/* Fire the unlock toast WITHOUT touching the server, so the overlay/icon/sound
 * work (#21/#22/#24) can be iterated without playing. Uses a real achievement
 * title from the loaded set if available (so it looks authentic); otherwise a
 * placeholder. Does NOT unlock anything on the user's RA account. */
void Port_RA_SimulateUnlock(void) {
    const char* title = "Test Achievement";
    if (sClient) {
        rc_client_achievement_list_t* list = rc_client_create_achievement_list(
            sClient, RC_CLIENT_ACHIEVEMENT_CATEGORY_CORE,
            RC_CLIENT_ACHIEVEMENT_LIST_GROUPING_PROGRESS);
        if (list && list->num_buckets > 0 &&
            list->buckets[0].num_achievements > 0) {
            const rc_client_achievement_t* a = list->buckets[0].achievements[0];
            if (a && a->title) {
                static char held[160];
                snprintf(held, sizeof held, "%s", a->title);
                title = held;
            }
        }
        if (list) {
            rc_client_destroy_achievement_list(list);
        }
    }
    char msg[200];
    snprintf(msg, sizeof msg, "Achievement: %s", title);
    Port_RA_Toast(msg);
    ralog("[ra] simulated unlock toast: %s\n", title);
}

/* ----- per-frame tick --------------------------------------------------- */
/* Call once per rendered frame. rc_client reads memory here and fires events
 * (unlocks) through the handler. No-op until a game is loaded. */
void Port_RA_DoFrame(void) {
    if (sClient) {
        rc_client_do_frame(sClient);
    }
}

/* ----- login ------------------------------------------------------------ */
/* On a successful login, stash the issued token so future boots can re-login
 * without asking for the password again, then load the game's achievement set. */
static void login_done(int result, const char* error_message, rc_client_t* client, void* userdata) {
    (void)userdata;
    if (result != RC_OK) {
        ralog("[ra] login failed (%d): %s\n", result, error_message ? error_message : "unknown");
        return;
    }
    const rc_client_user_t* user = rc_client_get_user_info(client);
    if (user && user->username && user->token) {
        save_token(user->username, user->token);
        ralog("[ra] logged in as %s (token saved)\n", user->username);
    }
    /* Identify and load the current game now that we're authenticated. */
    extern int Port_RA_LoadGame(void);
    Port_RA_LoadGame();
}

/*
 * Silent auto-login at boot: ONLY attempts a token re-login if a saved token
 * exists. Never prompts (no keyboard) and never blocks on user input — if there
 * is no token, it does nothing and returns 0 (RA simply stays logged out until
 * the user logs in from the menu). This keeps the boot path clean for users who
 * don't use RetroAchievements. Returns 1 if a token re-login was dispatched.
 *
 * Note: the server_call is synchronous (blocking curl), so a token re-login here
 * does briefly wait on the network — but only when a token already exists (the
 * user opted in), and it fails fast if offline. The interactive (keyboard) login
 * is deliberately NOT here; it's user-triggered via Port_RA_InteractiveLogin.
 */
int Port_RA_TryAutoLogin(void) {
    if (!sClient) {
        return 0;
    }
    char user[64], token[256];
    if (load_token(user, sizeof user, token, sizeof token)) {
        ralog("[ra] re-login with saved token for %s\n", user);
        rc_client_begin_login_with_token(sClient, user, token, login_done, NULL);
        return 1;
    }
    return 0;
}

/*
 * Interactive login, triggered by the user from the settings menu. Prompts for
 * the RA username + password via the native keyboard, logs in with the password
 * (which returns a token we then save for future silent re-logins). Returns 0 if
 * a login attempt was dispatched, negative if cancelled. Blocks while the
 * keyboard is open and during the (synchronous) login request — acceptable here
 * because the user explicitly asked for it.
 */
int Port_RA_InteractiveLogin(void) {
    if (!sClient) {
        return -1;
    }
    /* Clear, language-aware prompts shown inside the keyboard box, so it's
     * obvious which field is being asked for (the bare box confused testers). */
    extern int Port_Config_Language(void); /* 0=EN, 1=PT, 2=ES */
    int lang = Port_Config_Language();
    const char* userPrompt = (lang == 1) ? "Usuario do RetroAchievements"
                           : (lang == 2) ? "Usuario de RetroAchievements"
                                         : "RetroAchievements username";
    const char* passPrompt = (lang == 1) ? "Senha do RetroAchievements"
                           : (lang == 2) ? "Contrasena de RetroAchievements"
                                         : "RetroAchievements password";

    char user[64], password[128];
    if (!Port_Swkbd_Get(userPrompt, 0, user, sizeof user)) {
        ralog("[ra] login cancelled (username)\n");
        return -2;
    }
    if (!Port_Swkbd_Get(passPrompt, 1, password, sizeof password)) {
        ralog("[ra] login cancelled (password)\n");
        return -3;
    }
    ralog("[ra] logging in as %s\n", user);
    rc_client_begin_login_with_password(sClient, user, password, login_done, NULL);
    /* password stays only on the stack; it's never persisted. */
    return 0;
}

/* ----- game identification + load --------------------------------------- */
/* Completion callback for rc_client_begin_load_game. result == RC_OK means the
 * game was recognized and its achievement set is now active. */
static void load_game_done(int result, const char* error_message, rc_client_t* client, void* userdata) {
    (void)client;
    (void)userdata;
    if (result == RC_OK) {
        ralog("[ra] game loaded — achievement set active\n");
    } else {
        ralog("[ra] load game failed (%d): %s\n", result,
              error_message ? error_message : "unknown");
    }
}

/*
 * Identify the loaded ROM to RetroAchievements and load its achievement set.
 * RA hashes the whole GBA ROM (MD5); rcheevos does this for us via
 * rc_hash_generate_from_buffer with the GBA console id. Requires the user to be
 * logged in first (step 6). Returns 0 if the load was dispatched.
 */
int Port_RA_LoadGame(void) {
    if (!sClient) {
        return -1;
    }
    if (!gRomData || gRomSize == 0) {
        ralog("[ra] no ROM in memory to hash\n");
        return -2;
    }
    char hash[33];
    if (!rc_hash_generate_from_buffer(hash, RC_CONSOLE_GAMEBOY_ADVANCE, gRomData, gRomSize)) {
        ralog("[ra] hash generation failed\n");
        return -3;
    }
    ralog("[ra] ROM hash=%s (%u bytes)\n", hash, gRomSize);
    rc_client_begin_load_game(sClient, hash, load_game_done, NULL);
    return 0;
}

/* ----- lifecycle -------------------------------------------------------- */
/* Create the rc_client. Networking (Port_Net_Init) must already be up. Safe to
 * call once; returns 0 on success. Login/game-load come in later steps. */
int Port_RA_Init(void) {
    if (sClient) {
        return 0;
    }
    sClient = rc_client_create(read_memory, server_call);
    if (!sClient) {
        ralog("[ra] rc_client_create failed\n");
        return -1;
    }
    /* Softcore for now (no save-state lockout). */
    rc_client_set_hardcore_enabled(sClient, 0);
    rc_client_set_event_handler(sClient, event_handler);
    ralog("[ra] rc_client created (softcore)\n");
    return 0;
}

void Port_RA_Shutdown(void) {
    if (sClient) {
        rc_client_destroy(sClient);
        sClient = NULL;
    }
}

rc_client_t* Port_RA_Client(void) {
    return sClient;
}

/* ----- status (for the menu label) -------------------------------------- */
int Port_RA_IsLoggedIn(void) {
    return (sClient && rc_client_get_user_info(sClient) != NULL) ? 1 : 0;
}

const char* Port_RA_UserName(void) {
    if (!sClient) {
        return NULL;
    }
    const rc_client_user_t* user = rc_client_get_user_info(sClient);
    return user ? user->display_name : NULL;
}
