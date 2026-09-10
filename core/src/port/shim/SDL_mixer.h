/*
 * SDL_mixer.h  --  SDL2_mixer shim for the libdragon port.
 *
 * The vendored Sound.c keeps all of its channel / volume / resource
 * bookkeeping.  The calls that actually move samples are routed to the
 * libdragon `mixer` backend in src/port/pd_sound.c (see pd_sound.h).  Entry
 * points that the N64 port no longer uses (the in-memory RWops loaders --
 * assets are pre-converted .wav64 files loaded by resource id instead) stay
 * as harmless stubs.
 */
#ifndef PORT_SHIM_SDL_MIXER_H__
#define PORT_SHIM_SDL_MIXER_H__

#include <SDL.h>
#include <pd_sound.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque -- Sound.c only ever handles these by pointer; the pointer is a
 * PD_SoundLoad() handle. */
typedef struct Mix_Chunk Mix_Chunk;
typedef struct Mix_Music Mix_Music;

#define MIX_MAX_VOLUME     128
#define MIX_DEFAULT_FORMAT 0x8010

/* --- real, backed by pd_sound.c ------------------------------------------ */
int  Mix_PlayChannel(int ch, Mix_Chunk *c, int loops);
int  Mix_Playing(int ch);
int  Mix_HaltChannel(int ch);
int  Mix_Volume(int ch, int v);
int  Mix_VolumeChunk(Mix_Chunk *c, int v);
void Mix_FreeChunk(Mix_Chunk *c);

/* --- not needed on N64 (pd_sound owns device + asset loading) ----------- */
static inline int   Mix_OpenAudio(int a, Uint16 b, int c, int d) { (void)a;(void)b;(void)c;(void)d; return 0; }
static inline int   Mix_AllocateChannels(int n) { (void)n; return n; }
static inline int   Mix_VolumeMusic(int v) { (void)v; return v; }
static inline int   Mix_PlayMusic(Mix_Music *m, int loops) { (void)m;(void)loops; return 0; }
static inline int   Mix_PlayingMusic(void) { return 0; }
static inline int   Mix_HaltMusic(void) { return 0; }
static inline void  Mix_FreeMusic(Mix_Music *m) { (void)m; }
static inline Mix_Chunk *Mix_LoadWAV_RW(SDL_RWops *src, int freesrc) { if (src && freesrc) SDL_RWclose(src); return NULL; }
static inline Mix_Music *Mix_LoadMUS_RW(SDL_RWops *src, int freesrc) { if (src && freesrc) SDL_RWclose(src); return NULL; }
static inline Mix_Chunk *Mix_LoadWAV(const char *f) { (void)f; return NULL; }
static inline Mix_Music *Mix_LoadMUS(const char *f) { (void)f; return NULL; }
static inline void  Mix_Quit(void) { }
static inline const char *Mix_GetError(void) { return ""; }

#ifdef __cplusplus
}
#endif

#endif /* PORT_SHIM_SDL_MIXER_H__ */
