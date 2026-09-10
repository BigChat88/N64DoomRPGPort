/*
 * pd_sound.c  --  libdragon `mixer` + `wav64` backend behind the SDL_mixer /
 * FluidSynth shims.  See pd_sound.h.
 *
 * Channel layout mirrors the engine's Sound.c:
 *   0 .. MAX_SOUNDCHANNELS-1   SFX
 *   MAX_SOUNDCHANNELS          music (the slot Sound.c uses for FluidSynth)
 */
#include <libdragon.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "pd_sound.h"
#include <SDL_mixer.h>      /* Mix_Chunk / Mix_* prototypes we implement here */
#include <fluidsynth.h>     /* fluid_player_* prototypes + FLUID_PLAYER_* */

#define SND_FREQ        44100      /* mixer output rate                     */
#define SND_MAX_SRCFREQ 48000.0f   /* accept source samples up to this rate */
#define SND_BUFFERS     4
#define MAX_SFX_CH      10          /* == MAX_SOUNDCHANNELS */
#define MUSIC_CH        MAX_SFX_CH          /* engine MIDI-derived music     */
#define BGM_CH          (MAX_SFX_CH + 1)    /* per-level music (assets/doomOST) */
#define NUM_CH          (MAX_SFX_CH + 2)

typedef struct PD_Snd {
    wav64_t wav;
    int     loaded;
    int     is_music;
} PD_Snd;

static int    s_ready;
static int    s_sfx_vol   = 128;    /* 0..128 */
static float  s_music_gain = 1.0f;  /* 0..1   */
static int    s_music_loop_pending;
static PD_Snd *s_music_cur;
static PD_Snd *s_sfx_cur[MAX_SFX_CH];   /* what each SFX channel last played */
static int     s_sfx_voices;            /* active SFX count sfx_rebalance() last applied */

/* --- per-level background music (streamed Opus from rom:/mus/<map>.wav64) --- */
static wav64_t s_bgm;
static int     s_bgm_open;           /* s_bgm currently holds an open file   */
static int     s_bgm_enabled = 1;    /* the "Music" audio-menu toggle        */
static char    s_bgm_name[24];       /* basename of the open track, "" none  */

/* --- one-shot "stinger" on MUSIC_CH (the original level-up theme) ---------
 * While it plays the streamed background loop is stopped but kept open;
 * PD_SoundUpdate() restarts that loop from the top once the stinger ends. */
static PD_Snd *s_stinger;
static int     s_bgm_resume_after_stinger;

static void bgm_start_channel(void);   /* defined with the level-music block */
static void sfx_rebalance(int force);  /* defined with the SFX block          */

static void stinger_clear(void)
{
    s_stinger = NULL;
    s_bgm_resume_after_stinger = 0;
}

/* ------------------------------------------------------------ lifecycle */
void PD_SoundInit(void)
{
    /* Only bring the audio subsystem up if at least one converted asset is
     * actually present -- otherwise this is pure overhead for silence. */
    static const int probe[] = { 5039, 5042, 5040, 5043 };
    int have = 0;
    for (unsigned i = 0; i < sizeof(probe) / sizeof(probe[0]); i++) {
        char p[48];
        snprintf(p, sizeof(p), "/snd/%d.wav64", probe[i]);
        int fd = dfs_open(p);
        if (fd >= 0) { dfs_close(fd); have = 1; break; }
    }
    /* also bring audio up if only the per-level music was packed */
    static const char *const mprobe[] = { "junction", "intro", "level01" };
    for (unsigned i = 0; !have && i < sizeof(mprobe) / sizeof(mprobe[0]); i++) {
        char p[48];
        snprintf(p, sizeof(p), "/mus/%s.wav64", mprobe[i]);
        int fd = dfs_open(p);
        if (fd >= 0) { dfs_close(fd); have = 1; break; }
    }
    if (!have) {
        debugf("[pd_sound] no rom:/snd/*.wav64 -- audio disabled\n");
        return;
    }

    audio_init(SND_FREQ, SND_BUFFERS);
    mixer_init(NUM_CH);
    /* our .wav64 assets are Opus-compressed (wav64 compression level 3);
     * the decoder must be registered before the first wav64_open(). */
    wav64_init_compression(3);
    /* mixer_init caps every channel's source-frequency at the output rate;
     * raise it so higher-rate .wav64 assets (44.1k, 48k) don't assert. */
    for (int ch = 0; ch < NUM_CH; ch++)
        mixer_ch_set_limits(ch, 0, SND_MAX_SRCFREQ, 0);
    /* headroom: several SFX summing at full scale clip hard on the N64 DAC */
    mixer_set_vol(0.7f);
    s_ready = 1;
    debugf("[pd_sound] audio online @ %d Hz, %d channels\n", SND_FREQ, NUM_CH);
}

void PD_SoundShutdown(void)
{
    if (!s_ready) return;
    if (s_bgm_open) { wav64_close(&s_bgm); s_bgm_open = 0; s_bgm_name[0] = '\0'; }
    mixer_close();
    audio_close();
    s_ready = 0;
    s_music_cur = NULL;
    s_sfx_voices = 0;
    stinger_clear();
}

void PD_SoundUpdate(void)
{
    if (!s_ready) return;
    while (audio_can_write()) {
        short *buf = audio_write_begin();
        mixer_poll(buf, audio_get_buffer_length());
        audio_write_end();
    }

    /* voices that ended on their own free their headroom back to the rest */
    sfx_rebalance(0);

    /* the level-up stinger has finished playing on MUSIC_CH -- bring the
     * per-level background loop back from the start */
    if (s_stinger && !mixer_ch_playing(MUSIC_CH)) {
        int resume = s_bgm_resume_after_stinger;
        stinger_clear();
        if (resume && s_bgm_open && s_bgm_enabled && !mixer_ch_playing(BGM_CH))
            bgm_start_channel();
    }
}

/* ------------------------------------------------------------ asset load */
void *PD_SoundLoad(int resourceID, int isMusic)
{
    if (!s_ready) return NULL;

    char dfspath[48];
    snprintf(dfspath, sizeof(dfspath), "/snd/%d.wav64", resourceID);
    int fd = dfs_open(dfspath);
    if (fd < 0) return NULL;                 /* not converted yet -> silent */
    dfs_close(fd);

    PD_Snd *s = calloc(1, sizeof(*s));
    if (!s) return NULL;

    char rompath[56];
    snprintf(rompath, sizeof(rompath), "rom:%s", dfspath);
    wav64_open(&s->wav, rompath);
    s->loaded   = 1;
    s->is_music = isMusic;
    return s;
}

void PD_SoundFree(void *handle)
{
    PD_Snd *s = (PD_Snd *)handle;
    if (!s) return;
    if (s == s_music_cur || s == s_stinger) {
        if (s_ready) mixer_ch_stop(MUSIC_CH);
        s_music_cur = NULL;
        stinger_clear();
    }
    for (int c = 0; c < MAX_SFX_CH; c++)
        if (s_sfx_cur[c] == s) {
            if (s_ready && mixer_ch_playing(c)) mixer_ch_stop(c);
            s_sfx_cur[c] = NULL;
        }
    if (s->loaded) wav64_close(&s->wav);
    free(s);
}

/* ------------------------------------------------------------ SFX */
static float sfx_gain(void)
{
    int v = s_sfx_vol < 0 ? 0 : (s_sfx_vol > 128 ? 128 : s_sfx_vol);
    return (float)v / 128.0f;
}

/* Polyphony headroom.  The libdragon mixer sums every channel and the N64 DAC
 * hard-clips the sum, so several SFX at unity gain crackle -- most audible when
 * a cluster of enemies dies on the same frame.  Scale each live SFX voice by
 * ~1/sqrt(n): a lone sound stays at full level, four play at half each, which
 * keeps the summed peak roughly bounded without a per-sample limiter.  Cheap:
 * a 10-entry scan plus one mixer_ch_set_vol per active voice, and only when the
 * voice count actually changed (or `force`, for the volume slider). */
static void sfx_rebalance(int force)
{
    int n = 0;
    for (int c = 0; c < MAX_SFX_CH; c++) {
        if (s_sfx_cur[c] && mixer_ch_playing(c)) n++;
        else s_sfx_cur[c] = NULL;            /* voice ended -- release the slot */
    }
    if (!force && n == s_sfx_voices) return;
    s_sfx_voices = n;
    if (n == 0) return;

    /* poly[n] ~= 1/sqrt(n), clamped so one or two voices are barely touched */
    static const float poly[MAX_SFX_CH + 1] = {
        1.00f, 1.00f, 0.71f, 0.58f, 0.50f, 0.45f,
        0.41f, 0.38f, 0.35f, 0.33f, 0.32f,
    };
    float g = sfx_gain() * poly[n];
    for (int c = 0; c < MAX_SFX_CH; c++)
        if (s_sfx_cur[c] && mixer_ch_playing(c))
            mixer_ch_set_vol(c, g, g);
}

void PD_SfxPlay(int ch, void *handle, int loop)
{
    PD_Snd *s = (PD_Snd *)handle;
    if (!s_ready || !s || !s->loaded || ch < 0 || ch >= MAX_SFX_CH) return;

    /* Retrigger, don't stack: if this same sound is still ringing on another
     * channel (button mashing), stop that copy first.  Otherwise N identical
     * short waves sum well past full scale and the DAC clips into noise. */
    for (int c = 0; c < MAX_SFX_CH; c++)
        if (c != ch && s_sfx_cur[c] == s && mixer_ch_playing(c)) {
            mixer_ch_stop(c);
            s_sfx_cur[c] = NULL;
        }

    s_sfx_cur[ch] = s;
    wav64_set_loop(&s->wav, loop != 0);
    mixer_ch_play(ch, &s->wav.wave);
    sfx_rebalance(1);           /* re-spread headroom across the new voice count */
}

void PD_SfxStop(int ch)
{
    if (!s_ready || ch < 0 || ch >= MAX_SFX_CH) return;
    if (mixer_ch_playing(ch)) mixer_ch_stop(ch);
    s_sfx_cur[ch] = NULL;
    sfx_rebalance(0);          /* one voice fewer -- bring the rest back up */
}

int PD_SfxPlaying(int ch)
{
    if (!s_ready || ch < 0 || ch >= MAX_SFX_CH) return 0;
    return mixer_ch_playing(ch) ? 1 : 0;
}

void PD_SfxSetMasterVol(int vol_0_128)
{
    s_sfx_vol = vol_0_128;
    if (!s_ready) return;
    sfx_rebalance(1);
}

void PD_SfxSetChunkVol(void *handle, int vol_0_128)
{
    (void)handle; (void)vol_0_128;   /* libdragon volume is per-channel only */
}

/* ------------------------------------------------------------ music */
void PD_MusicPlay(void *handle, int loop)
{
    PD_Snd *s = (PD_Snd *)handle;
    stinger_clear();                 /* engine music takes the channel over */
    if (!s_ready || !s || !s->loaded) { s_music_cur = NULL; return; }
    wav64_set_loop(&s->wav, loop != 0);
    /* force the mixer to re-read the waveform config (loop length): it skips
     * that when the same wave object is replayed on the channel, so a shared
     * handle whose loop flag just changed would keep the previous setting */
    mixer_ch_stop(MUSIC_CH);
    mixer_ch_play(MUSIC_CH, &s->wav.wave);
    mixer_ch_set_vol(MUSIC_CH, s_music_gain, s_music_gain);
    s_music_cur = s;
}

void PD_MusicStop(void *handle)
{
    if (!s_ready) return;
    if (!handle || (PD_Snd *)handle == s_music_cur) {
        mixer_ch_stop(MUSIC_CH);
        s_music_cur = NULL;
    }
}

int PD_MusicPlaying(void *handle)
{
    if (!s_ready || !handle || (PD_Snd *)handle != s_music_cur) return 0;
    return mixer_ch_playing(MUSIC_CH) ? 1 : 0;
}

void PD_MusicSetGain(double gain_0_1)
{
    if (gain_0_1 < 0.0)      gain_0_1 = 0.0;
    else if (gain_0_1 > 1.0) gain_0_1 = 1.0;
    s_music_gain = (float)gain_0_1;
    if (!s_ready) return;
    if (s_music_cur && mixer_ch_playing(MUSIC_CH))
        mixer_ch_set_vol(MUSIC_CH, s_music_gain, s_music_gain);
    if (mixer_ch_playing(BGM_CH))
        mixer_ch_set_vol(BGM_CH, s_music_gain, s_music_gain);
}

/* Play `handle` once on the music channel as the original level-up theme.
 * The streamed per-level background loop is stopped (kept open) and restarted
 * from the top by PD_SoundUpdate() when the theme ends.  A NULL / not-ready
 * handle leaves the background music playing untouched. */
void PD_MusicStinger(void *handle)
{
    PD_Snd *s = (PD_Snd *)handle;
    if (!s_ready || !s || !s->loaded) return;

    /* an open track with music enabled is one that should be playing -- bring
     * it back after the theme regardless of a momentary gap in the loop */
    s_bgm_resume_after_stinger = s_bgm_open && s_bgm_enabled;
    if (mixer_ch_playing(BGM_CH)) mixer_ch_stop(BGM_CH);

    wav64_set_loop(&s->wav, false);
    /* 5043 is a shared handle the engine also plays as looping music; a bare
     * mixer_ch_play() would keep that channel's previous loop_len and the
     * stinger would never end.  Stop first so the config is re-read. */
    mixer_ch_stop(MUSIC_CH);
    mixer_ch_play(MUSIC_CH, &s->wav.wave);
    mixer_ch_set_vol(MUSIC_CH, s_music_gain, s_music_gain);
    s_music_cur = NULL;             /* not an engine fluid_player handle */
    s_stinger   = s;
}

void PD_MusicStingerStop(void)
{
    if (!s_stinger) return;
    int resume = s_bgm_resume_after_stinger;
    stinger_clear();
    if (s_ready && mixer_ch_playing(MUSIC_CH)) mixer_ch_stop(MUSIC_CH);
    s_music_cur = NULL;
    if (resume && s_bgm_open && s_bgm_enabled && !mixer_ch_playing(BGM_CH))
        bgm_start_channel();
}

/* ------------------------------------------------ per-level background music */
static void bgm_start_channel(void)
{
    wav64_set_loop(&s_bgm, true);
    mixer_ch_play(BGM_CH, &s_bgm.wave);
    mixer_ch_set_vol(BGM_CH, s_music_gain, s_music_gain);
}

void PD_LevelMusicStop(void)
{
    stinger_clear();
    if (s_ready && mixer_ch_playing(BGM_CH)) mixer_ch_stop(BGM_CH);
    if (s_bgm_open) { wav64_close(&s_bgm); s_bgm_open = 0; }
    s_bgm_name[0] = '\0';
}

/* Start (or keep) the track whose basename is `name`; NULL/"" stops music.
 * The file is opened even while muted so the menu toggle can start it
 * instantly; it is only routed to the mixer when enabled. */
void PD_LevelMusicPlay(const char *name)
{
    if (!s_ready || !name || !name[0]) { PD_LevelMusicStop(); return; }

    if (s_bgm_open && !strcmp(s_bgm_name, name)) {
        /* leave a running level-up stinger alone -- PD_SoundUpdate() brings
         * this track back on its own when the stinger finishes */
        if (s_bgm_enabled && !s_stinger && !mixer_ch_playing(BGM_CH)) bgm_start_channel();
        return;
    }

    PD_LevelMusicStop();

    char dfspath[40], rompath[48];
    snprintf(dfspath, sizeof(dfspath), "/mus/%s.wav64", name);
    int fd = dfs_open(dfspath);
    if (fd < 0) { debugf("[pd_sound] no %s (level music)\n", dfspath); return; }
    dfs_close(fd);

    snprintf(rompath, sizeof(rompath), "rom:%s", dfspath);
    wav64_open(&s_bgm, rompath);
    s_bgm_open = 1;
    strncpy(s_bgm_name, name, sizeof(s_bgm_name) - 1);
    s_bgm_name[sizeof(s_bgm_name) - 1] = '\0';

    if (s_bgm_enabled) bgm_start_channel();
}

void PD_LevelMusicEnable(int on)
{
    s_bgm_enabled = on ? 1 : 0;
    if (!s_ready) return;
    if (s_bgm_enabled) {
        if (s_bgm_open && !s_stinger && !mixer_ch_playing(BGM_CH)) bgm_start_channel();
    } else {
        s_bgm_resume_after_stinger = 0;
        if (mixer_ch_playing(BGM_CH)) mixer_ch_stop(BGM_CH);
    }
}

int PD_LevelMusicEnabled(void) { return s_bgm_enabled; }

void PD_LevelMusicForMap(int mapID)
{
    /* ids from src/doomrpg/Game.h: MAP_MENU=0, MAP_INTRO=1 .. MAP_END_GAME=13.
     * Index this table directly by the MAP_* id -- the leading NULL is the
     * MAP_MENU slot, without it every level was pulled one entry off and the
     * last two real levels (junction_destroyed, reactor) fell on NULL. */
    static const char *const track[] = {
        NULL,                  /*  0  MAP_MENU               */
        "intro",               /*  1  MAP_INTRO              */
        "level01",             /*  2  MAP_SECTOR01           */
        "level02", "level03", "level04", "level05", "level06", "level07",
        "junction",            /*  9  MAP_JUNCTION           */
        "junction_destroyed",  /* 10  MAP_JUNCTION_DESTROYED */
        NULL,                  /* 11  MAP_ITEMS  (debug map) */
        "reactor",             /* 12  MAP_REACTOR            */
        NULL,                  /* 13  MAP_END_GAME           */
    };
    const char *name = NULL;
    if (mapID >= 0 && mapID < (int)(sizeof(track) / sizeof(track[0])))
        name = track[mapID];
    PD_LevelMusicPlay(name);
}

/* ============================================================ shim bodies
 * These are declared (not defined) in src/port/shim/SDL_mixer.h and
 * src/port/shim/fluidsynth.h; the engine calls them by their SDL / FluidSynth
 * names and we route them here. */

int Mix_PlayChannel(int ch, Mix_Chunk *c, int loops)
{
    PD_SfxPlay(ch, c, loops);        /* loops: -1 == forever, 0 == once */
    return 0;
}
int  Mix_Playing(int ch)                  { return PD_SfxPlaying(ch); }
int  Mix_HaltChannel(int ch)              { PD_SfxStop(ch); return 0; }
int  Mix_Volume(int ch, int v)            { if (ch < 0) PD_SfxSetMasterVol(v); return v; }
int  Mix_VolumeChunk(Mix_Chunk *c, int v) { PD_SfxSetChunkVol(c, v); return v; }
void Mix_FreeChunk(Mix_Chunk *c)          { PD_SoundFree(c); }

int fluid_settings_setnum(fluid_settings_t *s, const char *key, double val)
{
    (void)s; (void)key;             /* only "synth.gain" is ever set */
    PD_MusicSetGain(val);
    return 0;
}
int fluid_player_set_loop(fluid_player_t *p, int loop)
{
    (void)p; s_music_loop_pending = loop; return 0;
}
int fluid_player_play(fluid_player_t *p)
{
    PD_MusicPlay(p, s_music_loop_pending != 0);
    return 0;
}
int fluid_player_stop(fluid_player_t *p)  { PD_MusicStop(p); return 0; }
int fluid_player_seek(fluid_player_t *p, int ticks) { (void)p; (void)ticks; return 0; }
int fluid_player_get_status(fluid_player_t *p)
{
    return PD_MusicPlaying(p) ? FLUID_PLAYER_PLAYING : FLUID_PLAYER_DONE;
}
