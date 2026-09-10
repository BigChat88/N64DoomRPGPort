/*
 * SDL_Video.h  --  libdragon port replacement for DoomRPG-RE's SDL_Video.h.
 *
 * Keeps the same public surface the game code expects (the sdlVideo /
 * sdlController / fluidSynth globals and the SDL_* helper prototypes) but the
 * implementation in src/port/pd_video.c targets the Nintendo 64 display.
 */
#ifndef SDL_VIDEO_H__
#define SDL_VIDEO_H__

#include <SDL.h>
#include <fluidsynth.h>

typedef struct SDLVideo_s
{
    SDL_Window   *window;      /* unused on N64, kept for struct compat */
    SDL_Renderer *renderer;    /* opaque token handed to the SDL_* shims */
    int      rendererW;
    int      rendererH;
    boolean  fullScreen;
    boolean  vSync;
    boolean  integerScaling;
    boolean  displaySoftKeys;
    int      resolutionIndex;
} SDLVideo_t;

extern SDLVideo_t sdlVideo;

void        SDL_InitVideo(void);
void        SDL_Close(void);
SDLVideo_t *SDL_GetVideo(void);
void        SDL_RenderDrawFillCircle(SDL_Renderer *renderer, int x, int y, int r);
void        SDL_RenderDrawCircle(SDL_Renderer *renderer, int x, int y, int r);

/* [n64 port] Allocate / free the software renderer's framebuffer as *uncached*
 * memory -- column-major span writes are far cheaper without dcache
 * write-allocate.  SDL_UpdateTexture() takes a cached view for the blit read. */
void       *PD_FbAlloc(unsigned long size);
void        PD_FbFree(void *p);

/* ---------------------------------------------------------------- audio */
typedef struct FluidSynth_s
{
    fluid_settings_t     *settings;
    fluid_synth_t        *synth;
    fluid_audio_driver_t *adriver;
} FluidSynth_t;

extern FluidSynth_t fluidSynth;

void SDL_InitAudio(void);
void SDL_CloseAudio(void);

/* ----------------------------------------------------------- controller */
typedef struct SDLController_s
{
    SDL_GameController *gGameController;
    SDL_Joystick      *gJoystick;
    SDL_Haptic        *gJoyHaptic;
    int deadZoneLeft;
    int deadZoneRight;
    /* N64 port: no keyboard, so the password/code prompt is driven with a
     * spinner (up/down change the current digit, A commits it).  This enables
     * the game's existing gamepad digit-entry path without pretending there is
     * a live SDL_GameController. */
    boolean padDigitEntry;
} SDLController_t;

extern SDLController_t sdlController;

typedef struct SDLVidModes_s
{
    int width, height;
} SDLVidModes_t;

extern SDLVidModes_t sdlVideoModes[14];

#define JOYSTICK_DEAD_ZONE 8000
int   SDL_GameControllerGetButtonID(void);
char *SDL_GameControllerGetNameButton(int id);
char *SDL_MouseGetNameButton(int id);
int   SDL_JoystickGetButtonID(void);

/* ------------------------------------------------------- port additions */
/* Present the game's RGB565 software framebuffer to the N64 display and
 * pump one frame of controller input.  Called from src/port/main.c. */
void PD_PresentFrame(void);
int  PD_PollKey(void);            /* returns an AVK_* code, 0 if none */
int  PD_WantsQuit(void);
void PD_Rumble(int durationMs);   /* pulse the Rumble Pak (clamped, ~ms) */
int  PD_RumbleSupported(void);    /* 1 if libdragon sees a Rumble Pak on port 1 */

/* Paint a fatal message on the N64 screen and halt (never returns). */
void PD_FatalMessage(const char *title, const char *msg);

/* [n64 port] per-level background music (assets/doomOST -> rom:/mus).
 * PD_LevelMusicForMap() is called from the map loader with a MAP_* id;
 * PD_LevelMusicEnable() is the "Music" audio-menu toggle (persisted in
 * Config).  Defined in src/port/pd_sound.c. */
void PD_LevelMusicForMap(int mapID);
void PD_LevelMusicEnable(int on);
int  PD_LevelMusicEnabled(void);

#endif /* SDL_VIDEO_H__ */
