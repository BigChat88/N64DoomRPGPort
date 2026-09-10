/*
 * SDL.h  --  Minimal SDL2 compatibility shim for the libdragon (Nintendo 64)
 *            port of DoomRPG-RE.
 *
 * DoomRPG-RE was written against SDL2, but it only uses SDL for:
 *   - libc-style helpers (SDL_malloc / SDL_snprintf / SDL_memcpy / ...)
 *   - byte-order helpers (SDL_SwapLE*)
 *   - the SDL_RWops file abstraction (used by the .zip loader)
 *   - a very small amount of blitting (software framebuffer + a few images)
 *
 * This header provides just enough of that surface to compile the vendored
 * game code unmodified.  The heavy lifting lives in src/port/pd_*.c.
 *
 * NOTE: the Nintendo 64 (MIPS VR4300) is big-endian, so SDL_SwapLE* actually
 * swaps here (on the original x86 build they were no-ops).
 */
#ifndef PORT_SHIM_SDL_H__
#define PORT_SHIM_SDL_H__

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ types */
typedef uint8_t   Uint8;
typedef int8_t    Sint8;
typedef uint16_t  Uint16;
typedef int16_t   Sint16;
typedef uint32_t  Uint32;
typedef int32_t   Sint32;
typedef uint64_t  Uint64;
typedef int64_t   Sint64;

typedef int SDL_bool;
#define SDL_FALSE 0
#define SDL_TRUE  1

#ifndef SDLCALL
#define SDLCALL
#endif

typedef void *(*SDL_malloc_func)(size_t);
typedef void *(*SDL_calloc_func)(size_t, size_t);
typedef void *(*SDL_realloc_func)(void *, size_t);
typedef void  (*SDL_free_func)(void *);

/* Opaque handles -- only ever used through pointers by the game code. */
typedef struct SDL_Window        SDL_Window;
typedef struct SDL_Renderer      SDL_Renderer;
typedef struct SDL_Texture       SDL_Texture;
typedef struct SDL_GameController SDL_GameController;
typedef struct SDL_Joystick      SDL_Joystick;
typedef struct SDL_Haptic        SDL_Haptic;

typedef struct SDL_Rect  { int x, y, w, h; } SDL_Rect;
typedef struct SDL_Point { int x, y; }       SDL_Point;

typedef struct SDL_Color { Uint8 r, g, b, a; } SDL_Color;
typedef struct SDL_Palette {
    int        ncolors;
    SDL_Color *colors;
} SDL_Palette;

typedef struct SDL_PixelFormat {
    Uint32       format;
    SDL_Palette *palette;
    Uint8        BitsPerPixel;
    Uint8        BytesPerPixel;
    Uint32       Rmask, Gmask, Bmask, Amask;
} SDL_PixelFormat;

typedef struct SDL_Surface {
    Uint32           flags;
    SDL_PixelFormat *format;
    int              w, h;
    int              pitch;
    void            *pixels;
    int              refcount;
    /* port extras */
    Uint32           colorkey;
    int              has_colorkey;
} SDL_Surface;

/* ------------------------------------------------------------- pixel fmt */
#define SDL_PIXELFORMAT_UNKNOWN   0
#define SDL_PIXELFORMAT_RGB565    1
#define SDL_PIXELFORMAT_RGBA5551  2
#define SDL_PIXELFORMAT_RGB888    3
#define SDL_PIXELFORMAT_ARGB8888  4
#define SDL_PIXELFORMAT_INDEX8    5

#define SDL_TEXTUREACCESS_STATIC     0
#define SDL_TEXTUREACCESS_STREAMING  1
#define SDL_TEXTUREACCESS_TARGET     2

/* DoomRPG-RE only calls SDL_BYTESPERPIXEL(SDL_PIXELFORMAT_RGB565) -> 2 */
#define SDL_BYTESPERPIXEL(fmt) 2

/* --------------------------------------------------------------- libc map */
#define SDL_malloc(n)          malloc((n))
#define SDL_calloc(n,s)        calloc((n),(s))
#define SDL_realloc(p,n)       realloc((p),(n))
#define SDL_free(p)            free((p))
#define SDL_memset(d,c,n)      memset((d),(c),(n))
#define SDL_memcpy(d,s,n)      memcpy((d),(s),(n))
#define SDL_memmove(d,s,n)     memmove((d),(s),(n))
#define SDL_memcmp(a,b,n)      memcmp((a),(b),(n))
#define SDL_strlen(s)          strlen((s))
#define SDL_strcmp(a,b)        strcmp((a),(b))
#define SDL_strncmp(a,b,n)     strncmp((a),(b),(n))
#define SDL_strcasecmp(a,b)    strcasecmp((a),(b))
#define SDL_strncasecmp(a,b,n) strncasecmp((a),(b),(n))
#define SDL_strcpy(d,s)        strcpy((d),(s))
#define SDL_strncpy(d,s,n)     strncpy((d),(s),(n))
#define SDL_strcat(d,s)        strcat((d),(s))
#define SDL_strchr(s,c)        strchr((s),(c))
#define SDL_strstr(a,b)        strstr((a),(b))
#define SDL_strdup(s)          strdup((s))
#define SDL_atoi(s)            atoi((s))
#define SDL_snprintf          snprintf
#define SDL_vsnprintf         vsnprintf
#define SDL_sscanf            sscanf
#define SDL_abs(x)            abs((x))
#ifndef SDL_min
#define SDL_min(a,b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef SDL_max
#define SDL_max(a,b) (((a) > (b)) ? (a) : (b))
#endif

/* --------------------------------------------------------- byte swapping */
static inline Uint16 SDL_Swap16(Uint16 x) { return (Uint16)((x << 8) | (x >> 8)); }
static inline Uint32 SDL_Swap32(Uint32 x) {
    return ((x << 24) & 0xff000000u) | ((x <<  8) & 0x00ff0000u) |
           ((x >>  8) & 0x0000ff00u) | ((x >> 24) & 0x000000ffu);
}
static inline Uint64 SDL_Swap64(Uint64 x) {
    return ((Uint64)SDL_Swap32((Uint32)x) << 32) | SDL_Swap32((Uint32)(x >> 32));
}

/* N64 is big-endian: little-endian data on disk must be swapped. */
#define SDL_SwapLE16(x)  SDL_Swap16((Uint16)(x))
#define SDL_SwapLE32(x)  SDL_Swap32((Uint32)(x))
#define SDL_SwapLE64(x)  SDL_Swap64((Uint64)(x))
#define SDL_SwapBE16(x)  ((Uint16)(x))
#define SDL_SwapBE32(x)  ((Uint32)(x))
#define SDL_SwapBE64(x)  ((Uint64)(x))

/* ------------------------------------------------------------- SDL_RWops */
typedef struct SDL_RWops SDL_RWops;
struct SDL_RWops {
    Sint64 (*size)(SDL_RWops *ctx);
    Sint64 (*seek)(SDL_RWops *ctx, Sint64 offset, int whence);
    size_t (*read)(SDL_RWops *ctx, void *ptr, size_t sz, size_t maxnum);
    size_t (*write)(SDL_RWops *ctx, const void *ptr, size_t sz, size_t num);
    int    (*close)(SDL_RWops *ctx);
    Uint32 type;
    struct {
        Uint8 *base;
        Uint8 *here;
        Uint8 *stop;
    } mem;
    void  *hidden_ptr;   /* FILE* or malloc'd buffer for write streams */
    Sint64 hidden_num;
};

#define SDL_RWSEEK_SET 0
#define SDL_RWSEEK_CUR 1
#define SDL_RWSEEK_END 2

SDL_RWops *SDL_RWFromFile(const char *file, const char *mode);
SDL_RWops *SDL_RWFromMem(void *mem, int size);
SDL_RWops *SDL_RWFromConstMem(const void *mem, int size);

/* [n64 port] Persistent saves live in cartridge SRAM, not a filesystem
 * (see src/port/pd_save.c).  Game_deleteSaveFiles() calls remove("Config")
 * &c.; route that to the store.  <stdio.h> is already included above, so its
 * own remove() prototype is out of the way. */
int PD_SaveRemove(const char *name);
#undef remove
#define remove(x) PD_SaveRemove(x)

#define SDL_RWsize(ctx)              (ctx)->size(ctx)
#define SDL_RWseek(ctx,off,whence)   (ctx)->seek((ctx),(off),(whence))
#define SDL_RWtell(ctx)              (ctx)->seek((ctx), 0, SDL_RWSEEK_CUR)
#define SDL_RWread(ctx,ptr,sz,n)     (ctx)->read((ctx),(ptr),(sz),(n))
#define SDL_RWwrite(ctx,ptr,sz,n)    (ctx)->write((ctx),(ptr),(sz),(n))
#define SDL_RWclose(ctx)             (ctx)->close(ctx)

/* -------------------------------------------------------------- surfaces */
SDL_Surface *SDL_LoadBMP_RW(SDL_RWops *src, int freesrc);
#define SDL_LoadBMP(file) SDL_LoadBMP_RW(SDL_RWFromFile((file), "rb"), 1)
void   SDL_FreeSurface(SDL_Surface *surface);
Uint32 SDL_MapRGB(const SDL_PixelFormat *fmt, Uint8 r, Uint8 g, Uint8 b);
int    SDL_SetColorKey(SDL_Surface *surface, int flag, Uint32 key);

/* ---------------------------------------------------------- render stubs */
/* Implemented in src/port/pd_video.c -- most are no-ops for milestone 1,
 * except the framebuffer present path and image blits. */
SDL_Texture *SDL_CreateTexture(SDL_Renderer *r, Uint32 fmt, int access, int w, int h);
SDL_Texture *SDL_CreateTextureFromSurface(SDL_Renderer *r, SDL_Surface *s);
void SDL_DestroyTexture(SDL_Texture *t);
int  SDL_UpdateTexture(SDL_Texture *t, const SDL_Rect *rect, const void *pixels, int pitch);
int  SDL_RenderCopy(SDL_Renderer *r, SDL_Texture *t, const SDL_Rect *src, const SDL_Rect *dst);
void SDL_RenderPresent(SDL_Renderer *r);
int  SDL_RenderClear(SDL_Renderer *r);
int  SDL_RenderSetClipRect(SDL_Renderer *r, const SDL_Rect *rect);
int  SDL_SetRenderDrawColor(SDL_Renderer *r, Uint8 red, Uint8 g, Uint8 b, Uint8 a);
int  SDL_SetRenderDrawBlendMode(SDL_Renderer *r, int blendMode);
int  SDL_RenderDrawLine(SDL_Renderer *r, int x1, int y1, int x2, int y2);
int  SDL_RenderDrawPoint(SDL_Renderer *r, int x, int y);
int  SDL_RenderDrawPoints(SDL_Renderer *r, const SDL_Point *points, int count);
int  SDL_RenderFillRect(SDL_Renderer *r, const SDL_Rect *rect);
int  SDL_RenderDrawRect(SDL_Renderer *r, const SDL_Rect *rect);
int  SDL_SetTextureBlendMode(SDL_Texture *t, int blendMode);
int  SDL_SetTextureColorMod(SDL_Texture *t, Uint8 r, Uint8 g, Uint8 b);
int  SDL_SetTextureAlphaMod(SDL_Texture *t, Uint8 alpha);
#define SDL_BLENDMODE_NONE  0
#define SDL_BLENDMODE_BLEND 1
#define SDL_BLENDMODE_ADD   2

/* --------------------------------------------------------------- timing */
Uint32 SDL_GetTicks(void);
void   SDL_Delay(Uint32 ms);

/* ------------------------------------------------- accepted-and-ignored */
#define SDL_HINT_RENDER_VSYNC        "SDL_RENDER_VSYNC"
#define SDL_HINT_RENDER_SCALE_QUALITY "SDL_RENDER_SCALE_QUALITY"
#define SDL_WINDOW_FULLSCREEN        0x00000001u
#define SDL_DISABLE                  0
#define SDL_ENABLE                   1

static inline int  SDL_SetHint(const char *n, const char *v) { (void)n; (void)v; return 1; }
static inline int  SDL_ShowCursor(int toggle) { (void)toggle; return 0; }
static inline int  SDL_SetWindowFullscreen(SDL_Window *w, Uint32 f) { (void)w; (void)f; return 0; }
static inline int  SDL_RenderSetIntegerScale(SDL_Renderer *r, int enable) { (void)r; (void)enable; return 0; }
static inline const char *SDL_GetScancodeName(int scancode) { (void)scancode; return ""; }

static inline int  SDL_NumJoysticks(void) { return 0; }
static inline int  SDL_IsGameController(int idx) { (void)idx; return 0; }
static inline int  SDL_GameControllerRumble(SDL_GameController *g, Uint16 lo, Uint16 hi, Uint32 ms) { (void)g;(void)lo;(void)hi;(void)ms; return 0; }
static inline int  SDL_HapticRumbleStop(SDL_Haptic *h) { (void)h; return 0; }
static inline int  SDL_HapticRumblePlay(SDL_Haptic *h, float strength, Uint32 length) { (void)h;(void)strength;(void)length; return 0; }

/* ----------------------------------------------------------- misc / log */
const char *SDL_GetError(void);
int  SDL_SetError(const char *fmt, ...);
void SDL_Log(const char *fmt, ...);
#define SDL_LogError(cat, ...)  SDL_Log(__VA_ARGS__)

/* Keyboard state: DoomRPG_getEventKey()/DoomRPG_setBind() take an SDL key
 * state array.  On N64 there is no keyboard; return an all-zero table so the
 * scanning loops simply find nothing.  Real input comes through pd_input.c. */
const Uint8 *SDL_GetKeyboardState(int *numkeys);

/* A generous set of scancode constants -- values are arbitrary but must be
 * distinct and fit inside the table returned above (see pd_video.c). */
enum {
    SDL_SCANCODE_UNKNOWN = 0,
    SDL_SCANCODE_A = 4,  SDL_SCANCODE_B, SDL_SCANCODE_C, SDL_SCANCODE_D,
    SDL_SCANCODE_E, SDL_SCANCODE_F, SDL_SCANCODE_G, SDL_SCANCODE_H,
    SDL_SCANCODE_I, SDL_SCANCODE_J, SDL_SCANCODE_K, SDL_SCANCODE_L,
    SDL_SCANCODE_M, SDL_SCANCODE_N, SDL_SCANCODE_O, SDL_SCANCODE_P,
    SDL_SCANCODE_Q, SDL_SCANCODE_R, SDL_SCANCODE_S, SDL_SCANCODE_T,
    SDL_SCANCODE_U, SDL_SCANCODE_V, SDL_SCANCODE_W, SDL_SCANCODE_X,
    SDL_SCANCODE_Y, SDL_SCANCODE_Z,
    SDL_SCANCODE_1 = 30, SDL_SCANCODE_2, SDL_SCANCODE_3, SDL_SCANCODE_4,
    SDL_SCANCODE_5, SDL_SCANCODE_6, SDL_SCANCODE_7, SDL_SCANCODE_8,
    SDL_SCANCODE_9, SDL_SCANCODE_0,
    SDL_SCANCODE_RETURN = 40, SDL_SCANCODE_ESCAPE, SDL_SCANCODE_BACKSPACE,
    SDL_SCANCODE_TAB, SDL_SCANCODE_SPACE,
    SDL_SCANCODE_RIGHT = 79, SDL_SCANCODE_LEFT, SDL_SCANCODE_DOWN, SDL_SCANCODE_UP,
    SDL_SCANCODE_KP_1 = 89, SDL_SCANCODE_KP_2, SDL_SCANCODE_KP_3, SDL_SCANCODE_KP_4,
    SDL_SCANCODE_KP_5, SDL_SCANCODE_KP_6, SDL_SCANCODE_KP_7, SDL_SCANCODE_KP_8,
    SDL_SCANCODE_KP_9, SDL_SCANCODE_KP_0,
    SDL_SCANCODE_LCTRL = 224, SDL_SCANCODE_LSHIFT, SDL_SCANCODE_LALT,
    SDL_SCANCODE_RCTRL = 228, SDL_SCANCODE_RSHIFT, SDL_SCANCODE_RALT,
    SDL_NUM_SCANCODES = 512
};

/* --------------------------------------------------------- message box */
#define SDL_arraysize(a) (sizeof(a) / sizeof((a)[0]))

typedef struct SDL_MessageBoxButtonData {
    Uint32      flags;
    int         buttonid;
    const char *text;
} SDL_MessageBoxButtonData;

typedef struct SDL_MessageBoxColor { Uint8 r, g, b; } SDL_MessageBoxColor;

typedef struct SDL_MessageBoxColorScheme {
    SDL_MessageBoxColor colors[5];
} SDL_MessageBoxColorScheme;

typedef struct SDL_MessageBoxData {
    Uint32                          flags;
    SDL_Window                     *window;
    const char                     *title;
    const char                     *message;
    int                             numbuttons;
    const SDL_MessageBoxButtonData  *buttons;
    const SDL_MessageBoxColorScheme *colorScheme;
} SDL_MessageBoxData;

enum {
    SDL_MESSAGEBOX_ERROR       = 0x00000010,
    SDL_MESSAGEBOX_WARNING     = 0x00000020,
    SDL_MESSAGEBOX_INFORMATION = 0x00000040
};
enum {
    SDL_MESSAGEBOX_COLOR_BACKGROUND = 0,
    SDL_MESSAGEBOX_COLOR_TEXT,
    SDL_MESSAGEBOX_COLOR_BUTTON_BORDER,
    SDL_MESSAGEBOX_COLOR_BUTTON_BACKGROUND,
    SDL_MESSAGEBOX_COLOR_BUTTON_SELECTED,
    SDL_MESSAGEBOX_COLOR_MAX
};

int SDL_ShowMessageBox(const SDL_MessageBoxData *messageboxdata, int *buttonid);
int SDL_ShowSimpleMessageBox(Uint32 flags, const char *title, const char *message, SDL_Window *window);

/* Init flags / subsystems -- accepted and ignored. */
#define SDL_INIT_VIDEO          0x00000020u
#define SDL_INIT_AUDIO          0x00000010u
#define SDL_INIT_JOYSTICK       0x00000200u
#define SDL_INIT_GAMECONTROLLER 0x00002000u
#define SDL_INIT_HAPTIC         0x00001000u
#define SDL_INIT_EVERYTHING     0x0000FFFFu
int  SDL_Init(Uint32 flags);
int  SDL_InitSubSystem(Uint32 flags);
void SDL_Quit(void);
void SDL_QuitSubSystem(Uint32 flags);

#ifdef __cplusplus
}
#endif

#endif /* PORT_SHIM_SDL_H__ */
