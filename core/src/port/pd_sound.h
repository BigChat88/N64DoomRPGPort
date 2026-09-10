/*
 * pd_sound.h  --  N64/libdragon audio backend for the DoomRPG-RE port.
 *
 * The vendored engine drives audio through SDL2_mixer (SFX) and FluidSynth
 * (MIDI music).  Those headers are shimmed (src/port/shim/) and forward the
 * calls that matter here, onto libdragon's `mixer` + `wav64`.
 *
 * Assets are pre-converted PCM packed into the ROM as `rom:/snd/<id>.wav64`
 * (see the Makefile / assets/snd).  Any id that has not been converted yet
 * loads as NULL and plays as silence, so audio can be filled in piecemeal.
 */
#ifndef PD_SOUND_H__
#define PD_SOUND_H__

#ifdef __cplusplus
extern "C" {
#endif

/* lifecycle -- called from SDL_InitAudio / SDL_CloseAudio (pd_video.c) and
 * once per game loop from main.c */
void  PD_SoundInit(void);
void  PD_SoundShutdown(void);
void  PD_SoundUpdate(void);          /* feed the mixer into the audio DACs */

/* asset handles (opaque; one per soundTable[] entry) */
void *PD_SoundLoad(int resourceID, int isMusic);   /* NULL if not on disc */
void  PD_SoundFree(void *handle);

/* SFX -- mixer channels 0 .. MAX_SOUNDCHANNELS-1 */
void  PD_SfxPlay(int ch, void *handle, int loop);
void  PD_SfxStop(int ch);
int   PD_SfxPlaying(int ch);
void  PD_SfxSetMasterVol(int vol_0_128);
void  PD_SfxSetChunkVol(void *handle, int vol_0_128);

/* MIDI music -- one reserved mixer channel */
void  PD_MusicPlay(void *handle, int loop);
void  PD_MusicStop(void *handle);      /* NULL handle == stop whatever plays */
int   PD_MusicPlaying(void *handle);
void  PD_MusicSetGain(double gain_0_1);

/* Play `handle` once as the original level-up theme: the streamed per-level
 * background music stops for its duration and resumes from the top after. */
void  PD_MusicStinger(void *handle);

/* Cut a playing level-up theme short and bring the background music straight
 * back -- called when the level-up status box is closed.  No-op otherwise. */
void  PD_MusicStingerStop(void);

/* Per-level background music -- streamed Opus from rom:/mus/<map>.wav64,
 * its own mixer channel, independent of the engine's SFX/music.  Driven by
 * the map loader (PD_LevelMusicForMap) and the "Music" audio-menu toggle. */
void  PD_LevelMusicForMap(int mapID);
void  PD_LevelMusicPlay(const char *name);   /* NULL / "" == stop */
void  PD_LevelMusicStop(void);
void  PD_LevelMusicEnable(int on);
int   PD_LevelMusicEnabled(void);

#ifdef __cplusplus
}
#endif

#endif /* PD_SOUND_H__ */
