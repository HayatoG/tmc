/*
 * switch_render_pool.c — a PERSISTENT libnx worker pool for the per-frame PPU
 * scanline render.
 *
 * The asset extraction uses switch_parallel_for (switch_parallel.c), which
 * spawns + joins threads per call — fine for a one-shot job, but creating
 * threads 60x/second for rendering would add per-frame latency and jitter.
 * This pool creates its worker threads ONCE and feeds them a "run body(ctx, i)
 * for i in [0,total)" job each frame via a generation counter + condvars, with
 * atomic work-stealing so uneven per-line cost still balances.
 *
 * Usage (from the render thread only — single producer):
 *     switch_render_pool_run(160, render_one_line, &ctx);
 * The first call lazily spawns the workers. The calling (main) thread also
 * participates as a worker, so total parallelism = spawned workers + 1.
 */
#include <switch.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>

/* Application mode exposes cores 0-2 (core 3 = OS). WORKERS = 0 → fully serial
 * render on the main thread, spawning NO worker threads, which leaves cores 1
 * AND 2 entirely free for the SDL audio mixing/callback thread. The render
 * worker threads (2, then 1) competed with audio for cores 1/2 and starved it
 * at the heavier widescreen width → crackle + audio slowdown. Since the PPU
 * render is NOT the fps bottleneck (the single-threaded game logic is — see
 * PERF_DOSSIER.md), serial render costs little fps but gives audio clean cores.
 * (Raise this again only if a future profile shows render is the bottleneck.) */
#define POOL_MAX_WORKERS 0

static Thread sWorkers[POOL_MAX_WORKERS];
static int sNumWorkers = -1; /* -1 = uninitialized, 0 = serial fallback */

static Mutex sMutex;
static CondVar sStart; /* workers wait here for a new job generation */
static CondVar sDone;  /* the producer waits here for all workers to finish */

static void (*sBody)(void *, size_t);
static void *sCtx;
static size_t sTotal;
static atomic_size_t sNext;   /* next index to claim (work-stealing) */
static atomic_int sActive;    /* workers still draining the queue */
static uint64_t sGen;         /* job generation; bumped per run */
static bool sShutdown;

static void DrainQueue(void) {
    size_t i;
    while ((i = atomic_fetch_add(&sNext, 1)) < sTotal) {
        sBody(sCtx, i);
    }
}

static void WorkerMain(void *arg) {
    (void)arg;
    uint64_t seen = 0;
    for (;;) {
        mutexLock(&sMutex);
        while (sGen == seen && !sShutdown) {
            condvarWait(&sStart, &sMutex);
        }
        if (sShutdown) {
            mutexUnlock(&sMutex);
            return;
        }
        seen = sGen;
        mutexUnlock(&sMutex);

        DrainQueue();

        /* Last worker to finish wakes the producer. */
        if (atomic_fetch_sub(&sActive, 1) == 1) {
            mutexLock(&sMutex);
            condvarWakeOne(&sDone);
            mutexUnlock(&sMutex);
        }
    }
}

static void PoolInit(void) {
    mutexInit(&sMutex);
    condvarInit(&sStart);
    condvarInit(&sDone);
    sGen = 0;
    sShutdown = false;
    atomic_store(&sNext, 0);
    atomic_store(&sActive, 0);

    int spawned = 0;
    for (int w = 0; w < POOL_MAX_WORKERS; w++) {
        /* 64 KB stack; priority just below the main thread's default (0x2C);
         * pin to cores 1, 2. */
        if (R_SUCCEEDED(threadCreate(&sWorkers[w], WorkerMain, NULL, NULL,
                                     64 * 1024, 0x2C, 1 + w)) &&
            R_SUCCEEDED(threadStart(&sWorkers[w]))) {
            spawned++;
        } else {
            break;
        }
    }
    sNumWorkers = spawned;

    /* One-shot diagnostic: confirm how many workers actually spawned (a failed
     * threadCreate would silently fall back to serial = no fps gain). cwd is
     * sdmc:/switch/tmc (port_main chdir'd there before AgbMain). */
    {
        FILE *f = fopen("render_pool.log", "w");
        if (f != NULL) {
            fprintf(f, "render pool: %d worker(s) spawned + main thread = %d-way parallel\n",
                    spawned, spawned + 1);
            fclose(f);
        }
    }
}

void switch_render_pool_run(size_t total, void (*body)(void *, size_t), void *ctx) {
    if (sNumWorkers < 0) {
        PoolInit();
    }
    if (sNumWorkers == 0 || total == 0) {
        /* No workers (spawn failed) — run serially on the caller. */
        for (size_t i = 0; i < total; i++) {
            body(ctx, i);
        }
        return;
    }

    sBody = body;
    sCtx = ctx;
    sTotal = total;
    atomic_store(&sNext, 0);
    atomic_store(&sActive, sNumWorkers);

    mutexLock(&sMutex);
    sGen++;
    condvarWakeAll(&sStart);
    mutexUnlock(&sMutex);

    /* The producer participates as an extra worker. */
    DrainQueue();

    /* Wait for the spawned workers to finish their in-flight bodies. */
    mutexLock(&sMutex);
    while (atomic_load(&sActive) > 0) {
        condvarWait(&sDone, &sMutex);
    }
    mutexUnlock(&sMutex);
}
