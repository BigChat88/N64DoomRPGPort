/*
 * pd_perf.c  --  see pd_perf.h.
 *
 * get_ticks_us() is a free-running 64-bit microsecond clock; truncating the
 * begin/now pair to 32 bits is fine because a frame delta is only tens of
 * thousands of microseconds and unsigned subtraction wraps correctly.
 */
#include <libdragon.h>
#include "pd_perf.h"

#define NAVG 30

static uint32_t s_begin[PD_PERF_NSLOTS];
static uint32_t s_accum[PD_PERF_NSLOTS];        /* summed within the frame   */
static uint32_t s_hist[PD_PERF_NSLOTS][NAVG];   /* last NAVG frames per slot */
static int      s_head;
static PD_PerfStats s_stats;

void PD_PerfBegin(int slot)
{
    if ((unsigned)slot < PD_PERF_NSLOTS)
        s_begin[slot] = (uint32_t)get_ticks_us();
}

void PD_PerfEnd(int slot)
{
    if ((unsigned)slot < PD_PERF_NSLOTS)
        s_accum[slot] += (uint32_t)get_ticks_us() - s_begin[slot];
}

void PD_PerfFrame(void)
{
    for (int i = 0; i < PD_PERF_NSLOTS; i++) {
        s_hist[i][s_head] = s_accum[i];
        s_accum[i] = 0;

        uint64_t sum = 0;
        for (int j = 0; j < NAVG; j++)
            sum += s_hist[i][j];
        s_stats.us[i] = (uint32_t)(sum / NAVG);
    }
    s_head = (s_head + 1) % NAVG;

    /* Real present-to-present rate from libdragon's own frame timer.  The
     * TOTAL slot no longer bounds the wall-clock frame (audio is now pumped
     * mid-render and overlaps), so derive fps from the display instead. */
    float f = display_get_fps();
    s_stats.fps_x100 = (uint32_t)(f * 100.0f + 0.5f);
}

const PD_PerfStats *PD_PerfStatsGet(void) { return &s_stats; }
