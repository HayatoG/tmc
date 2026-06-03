/*
 * switch_perf.c — tiny frame profiler to decide whether the core-0 budget is
 * spent in the (parallelizable) PPU render or the (serial) game logic + present.
 *
 * We can't reach <switch.h>'s svcGetSystemTick from the C++/GBA TUs, so the
 * timing primitive lives here and is driven from port_ppu.cpp via three marks:
 *   Port_Perf_FrameStart()  — top of Port_PPU_PresentFrame (≈ once per frame)
 *   Port_Perf_AfterPPU()    — right after virtuappu_render_frame()
 *   Port_Perf_FrameEnd()    — after SDL present
 * The gap FrameStart→prev FrameStart is the whole frame (includes game logic
 * that runs between presents); FrameStart→AfterPPU is the PPU render; the rest
 * is present/upload. Averages are logged to sdmc:/switch/tmc/perf.log every
 * 120 frames. Gated by TMC_PERF so it costs nothing in normal builds.
 */
#include <switch.h>
#include <stdio.h>

#ifdef TMC_PERF

static FILE* sPerfLog = NULL;
static u64 sTickStart = 0, sTickPPU = 0, sPrevStart = 0;
static u64 sAccFrame = 0, sAccPPU = 0, sAccPresent = 0;
static int sFrames = 0;

/* libnx system counter runs at 19.2 MHz → 19.2 ticks per microsecond. */
static u64 ticks_to_us(u64 t) {
    return t / 19ull; /* ~µs (close enough for a coarse profile) */
}

void Port_Perf_FrameStart(void) {
    u64 now = armGetSystemTick();
    if (sPrevStart != 0) {
        sAccFrame += (now - sPrevStart);
    }
    sPrevStart = now;
    sTickStart = now;
}

void Port_Perf_AfterPPU(void) {
    sTickPPU = armGetSystemTick();
    sAccPPU += (sTickPPU - sTickStart);
}

void Port_Perf_FrameEnd(void) {
    u64 now = armGetSystemTick();
    sAccPresent += (now - sTickPPU);
    if (++sFrames >= 120) {
        if (sPerfLog == NULL) {
            sPerfLog = fopen("perf.log", "w");
            if (sPerfLog) setvbuf(sPerfLog, NULL, _IONBF, 0);
        }
        if (sPerfLog) {
            u64 f = ticks_to_us(sAccFrame / sFrames);
            u64 p = ticks_to_us(sAccPPU / sFrames);
            u64 pr = ticks_to_us(sAccPresent / sFrames);
            fprintf(sPerfLog,
                "[perf] frame=%lluus ppu=%lluus present=%lluus logic=%lluus (ppu %llu%% of frame)\n",
                (unsigned long long)f, (unsigned long long)p, (unsigned long long)pr,
                (unsigned long long)(f > p + pr ? f - p - pr : 0),
                (unsigned long long)(f ? (p * 100 / f) : 0));
        }
        sAccFrame = sAccPPU = sAccPresent = 0;
        sFrames = 0;
    }
}

#endif /* TMC_PERF */
