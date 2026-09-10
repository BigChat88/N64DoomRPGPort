/*
 * pd_perf.h  --  lightweight per-frame CPU-time profiler for the N64 port.
 *
 * Phase 0 of the performance work: measure where the frame goes before
 * changing anything.  DoomCanvas / main.c bracket each phase of the frame
 * with PD_PerfBegin()/PD_PerfEnd(); once per displayed frame PD_PerfFrame()
 * rolls the samples into a 30-frame average that PD_PerfStatsGet() returns.
 *
 * Sampling is always on and costs a handful of cycles per bracket (two reads
 * of the CPU cycle counter); the in-game overlay that shows the numbers is
 * toggled from the Developer menu (r_perf -> DoomCanvas.perfOverlay).
 */
#ifndef PORT_PD_PERF_H__
#define PORT_PD_PERF_H__

enum {
    PD_PERF_TOTAL = 0,   /* whole game-loop step: logic + render + present   */
    PD_PERF_RENDER,      /* Render_render total (== FLOORCEIL + BSP + setup)  */
    PD_PERF_FLOORCEIL,   /* Render_renderFloorAndCeiling* (sub-part of RENDER)*/
    PD_PERF_BSP,         /* Render_renderBSP total (WALK + WALLS + SPRITES)   */
    PD_PERF_WALK,        /* Render_walkNode: BSP traversal / transform / clip */
    PD_PERF_WALLS,       /* Render_drawNodeLines: wall column spans           */
    PD_PERF_SPRITES,     /* Render_renderSpriteObject loop                   */
    PD_PERF_BLIT,        /* engine framebuffer -> screen copy (drawRGB)       */
    PD_PERF_HUD,         /* HUD bars / effects / text                        */
    PD_PERF_SOUND,       /* mixer_poll, summed over the frame                */
    PD_PERF_NSLOTS
};

typedef struct {
    unsigned us[PD_PERF_NSLOTS];   /* rolling-average microseconds per slot  */
    unsigned fps_x100;             /* frames/sec * 100, derived from TOTAL    */
} PD_PerfStats;

#ifdef __cplusplus
extern "C" {
#endif

void PD_PerfBegin(int slot);
void PD_PerfEnd(int slot);          /* adds (now - begin) into this frame     */
void PD_PerfFrame(void);            /* call once per displayed frame          */
const PD_PerfStats *PD_PerfStatsGet(void);

#ifdef __cplusplus
}
#endif

#endif /* PORT_PD_PERF_H__ */
