/*
 * main.c  --  Nintendo 64 / libdragon entry point for the DoomRPG-RE port.
 *
 * Replaces the SDL desktop Main.c: same boot sequence (zone init, video,
 * audio, open DoomRPG.zip, DoomRPG_Init) and the same ~15 ms game-loop
 * cadence, but input comes from the N64 controller via pd_input.c.
 */
#include <libdragon.h>
#include <SDL.h>

/* libdragon pulls in <stdbool.h>; DoomRPG.h defines its own
 * `enum { false, true } boolean`, which the macros would break. */
#undef true
#undef false
#undef bool

#include "DoomRPG.h"
#include "DoomCanvas.h"
#include "Z_Zone.h"
#include "Z_Zip.h"
#include "SDL_Video.h"
#include "pd_perf.h"

extern DoomRPG_t *doomRpg;

void PD_InputPoll(void);
void PD_PlayIntro(void);
void PD_SoundUpdate(void);
void PD_SaveInit(void);

int main(void)
{
    /* libdragon plumbing ------------------------------------------------ */
    debug_init_isviewer();
    debug_init_usblog();

    if (dfs_init(DFS_DEFAULT_LOCATION) != DFS_ESUCCESS) {
        debugf("FATAL: could not mount DragonFS (asset image missing?)\n");
        PD_FatalMessage("BOOT", "could not mount DragonFS\n(asset image missing?)");
    }
    joypad_init();

    /* Bring the FlashRAM save store up before the game reads its config. */
    PD_SaveInit();

    /* game boot ------------------------------------------------------- */
    Z_Init();
    SDL_InitVideo();
    SDL_InitAudio();

    PD_PlayIntro();

    debugf("main: openZipFile\n");
    openZipFile("DoomRPG.zip", &zipFile);

    debugf("main: DoomRPG_Init\n");
    if (DoomRPG_Init() == 0) {
        DoomRPG_Error("Failed to initialize Doom RPG\n");
    }
    debugf("main: entering game loop\n");

    /* main loop ------------------------------------------------------- */
    int upTime = 0;

    while (doomRpg->closeApplet != true) {
        int now = (int)DoomRPG_GetUpTimeMS();

        PD_InputPoll();
        int key;
        while ((key = PD_PollKey()) != 0) {
            DoomCanvas_keyPressed(doomRpg->doomCanvas, key);
        }

        if (now > upTime) {
            upTime = now + 15;
            PD_PerfBegin(PD_PERF_TOTAL);
            DoomRPG_loopGame(doomRpg);
            PD_PerfEnd(PD_PERF_TOTAL);
            PD_PerfFrame();     /* roll this frame's samples into the average */
        }

        PD_PerfBegin(PD_PERF_SOUND);
        PD_SoundUpdate();       /* keep the audio DACs fed */
        PD_PerfEnd(PD_PERF_SOUND);
    }

    closeZipFile(&zipFile);
    DoomRPG_FreeAppData(doomRpg);
    SDL_CloseAudio();
    SDL_Close();
    return 0;
}
