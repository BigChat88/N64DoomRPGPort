/*
 * fluidsynth.h  --  FluidSynth shim for the libdragon port.
 *
 * DoomRPG-RE routes MIDI music through FluidSynth on the PC build.  The N64
 * has no soft-synth of that size, so music is pre-rendered to PCM and played
 * back through libdragon's `mixer` (src/port/pd_sound.c).  The player entry
 * points below are real and route there; the synth/settings/driver
 * constructors are stubs (Sound.c keeps the pointers but never dereferences
 * them on this target).
 */
#ifndef PORT_SHIM_FLUIDSYNTH_H__
#define PORT_SHIM_FLUIDSYNTH_H__

#include <stddef.h>
#include <pd_sound.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct fluid_settings_t      fluid_settings_t;
typedef struct fluid_synth_t         fluid_synth_t;
typedef struct fluid_audio_driver_t  fluid_audio_driver_t;
typedef struct fluid_player_t        fluid_player_t;

enum {
    FLUID_PLAYER_READY = 0,
    FLUID_PLAYER_PLAYING,
    FLUID_PLAYER_STOPPING,
    FLUID_PLAYER_DONE
};

/* --- stubs: no soft-synth on N64 -------------------------------------- */
static inline fluid_settings_t     *new_fluid_settings(void) { return (fluid_settings_t *)0; }
static inline fluid_synth_t        *new_fluid_synth(fluid_settings_t *s) { (void)s; return (fluid_synth_t *)0; }
static inline fluid_audio_driver_t *new_fluid_audio_driver(fluid_settings_t *s, fluid_synth_t *y) { (void)s;(void)y; return (fluid_audio_driver_t *)0; }
static inline fluid_player_t       *new_fluid_player(fluid_synth_t *s) { (void)s; return (fluid_player_t *)0; }

static inline void delete_fluid_settings(fluid_settings_t *s) { (void)s; }
static inline void delete_fluid_synth(fluid_synth_t *s) { (void)s; }
static inline void delete_fluid_audio_driver(fluid_audio_driver_t *d) { (void)d; }
static inline void delete_fluid_player(fluid_player_t *p) { (void)p; }

static inline int  fluid_is_soundfont(const char *f) { (void)f; return 0; }
static inline int  fluid_synth_sfload(fluid_synth_t *s, const char *f, int reset) { (void)s;(void)f;(void)reset; return -1; }
static inline int  fluid_player_add_mem(fluid_player_t *p, const void *buf, size_t len) { (void)p;(void)buf;(void)len; return 0; }

/* --- real, backed by pd_sound.c ------------------------------------------ */
int  fluid_settings_setnum(fluid_settings_t *s, const char *k, double v);
int  fluid_player_play(fluid_player_t *p);
int  fluid_player_stop(fluid_player_t *p);
int  fluid_player_seek(fluid_player_t *p, int ticks);
int  fluid_player_set_loop(fluid_player_t *p, int loop);
int  fluid_player_get_status(fluid_player_t *p);

#ifdef __cplusplus
}
#endif

#endif /* PORT_SHIM_FLUIDSYNTH_H__ */
