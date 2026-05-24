/*
 * libnx-thread helpers for the on-device asset extraction.
 *
 * devkitA64's libstdc++ std::thread/std::async is unreliable (crashes silently
 * a few seconds in), so the asset pipeline can't use its std::thread parallel
 * path. These C helpers use the stable libnx thread API instead:
 *
 *   switch_parallel_for  — work-stealing parallel-for across up to 3 cores
 *                          (Application mode exposes cores 0-2; core 3 is the
 *                          OS). Used by PortAssetLog::ParallelFor on Switch.
 *   switch_bg_*          — run a function on a background thread with a
 *                          non-blocking done flag, so the main thread can keep
 *                          pumping the applet + drawing the progress bar.
 *
 * Kept in its own TU so <switch.h> (which defines u8/u32 etc.) doesn't clash
 * with the game headers used elsewhere.
 */
#include <switch.h>
#include <stdatomic.h>
#include <stdlib.h>

/* ---- parallel-for ------------------------------------------------------- */

typedef struct {
    size_t total;
    atomic_size_t next;
    void (*body)(void *, size_t);
    void *ctx;
} pf_shared;

static void pf_worker(void *p) {
    pf_shared *s = (pf_shared *)p;
    for (;;) {
        size_t i = atomic_fetch_add(&s->next, 1);
        if (i >= s->total) {
            return;
        }
        s->body(s->ctx, i);
    }
}

/* Runs body(ctx, i) for i in [0, total) across `nthreads` (1..3) workers.
 * The caller participates as one worker, so we spawn nthreads-1 extra libnx
 * threads. Index distribution is atomic work-stealing, so uneven per-item cost
 * still balances. If a thread can't be created (e.g. applet mode with fewer
 * cores), it degrades to fewer workers. */
void switch_parallel_for(size_t total, int nthreads,
                         void (*body)(void *, size_t), void *ctx) {
    if (total == 0) {
        return;
    }
    if (nthreads < 1) nthreads = 1;
    if (nthreads > 2) nthreads = 2; /* leave a core free for the main thread + OS */

    pf_shared sh;
    sh.total = total;
    atomic_init(&sh.next, 0);
    sh.body = body;
    sh.ctx = ctx;

    if (nthreads == 1) {
        pf_worker(&sh);
        return;
    }

    Thread th[2];
    int extra = nthreads - 1; /* 1 or 2 */
    int started = 0;
    for (int i = 0; i < extra; i++) {
        /* 2 MB stack: the asset-pipeline body (gfx decode, fmt, nlohmann::json,
         * std::filesystem) has deep, large frames — 512 KB overflowed and
         * crashed the console (the serial path ran on a 1 MB thread and was
         * fine). LOW priority (0x3B, below the main thread's 0x2C) so the main
         * thread and OS always preempt — keeps the bar/HOME/sleep responsive.
         * Core 1 (+ the caller on core 0) = 2 cores; core 2 stays free for the
         * main thread and OS. */
        Result rc = threadCreate(&th[i], pf_worker, &sh, NULL,
                                 2 * 1024 * 1024, 0x3B, i + 1);
        if (R_FAILED(rc)) {
            break;
        }
        if (R_FAILED(threadStart(&th[i]))) {
            threadClose(&th[i]);
            break;
        }
        started++;
    }

    pf_worker(&sh); /* caller participates */

    for (int i = 0; i < started; i++) {
        threadWaitForExit(&th[i]);
        threadClose(&th[i]);
    }
}

/* ---- background runner -------------------------------------------------- */

typedef struct {
    void (*fn)(void *);
    void *arg;
    volatile int done;
    Thread t;
    int ok;
} bg_task;

static void bg_entry(void *p) {
    bg_task *b = (bg_task *)p;
    b->fn(b->arg);
    b->done = 1;
}

/* Start fn(arg) on a background thread. Returns an opaque handle, or NULL on
 * failure (caller should then run fn synchronously). */
void *switch_bg_start(void (*fn)(void *), void *arg) {
    bg_task *b = (bg_task *)malloc(sizeof(bg_task));
    if (!b) {
        return NULL;
    }
    b->fn = fn;
    b->arg = arg;
    b->done = 0;
    b->ok = 0;
    /* 4 MB stack: runs the whole extraction pipeline (orchestration + the
     * heavy per-asset work when it participates as a worker). LOW priority
     * (0x3B) so the main thread (0x2C) and OS always win — UI/sleep stay
     * responsive. cpuid 0. */
    if (R_FAILED(threadCreate(&b->t, bg_entry, b, NULL, 4 * 1024 * 1024, 0x3B, 0))) {
        free(b);
        return NULL;
    }
    if (R_FAILED(threadStart(&b->t))) {
        threadClose(&b->t);
        free(b);
        return NULL;
    }
    b->ok = 1;
    return b;
}

int switch_bg_done(void *h) {
    return h ? ((bg_task *)h)->done : 1;
}

void switch_bg_join(void *h) {
    if (!h) {
        return;
    }
    bg_task *b = (bg_task *)h;
    threadWaitForExit(&b->t);
    threadClose(&b->t);
    free(b);
}
