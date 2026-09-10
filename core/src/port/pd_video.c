/*
 * pd_video.c  --  libdragon video backend for the DoomRPG-RE N64 port.
 *
 * DoomRPG-RE renders everything in software into a 16bpp RGB565 framebuffer it
 * owns (render->framebuffer).  The only things it asks "SDL" to do are:
 *
 *   SDL_CreateTexture(RGB565, STREAMING)      -- staging handle for that buffer
 *   SDL_UpdateTexture(tex, pixels, pitch)     -- hand us the current pixels
 *   SDL_RenderCopy(tex, src, dst)             -- blit staging OR an image
 *   SDL_RenderClear / SDL_RenderPresent       -- frame boundaries
 *   SDL_RenderSetClipRect / draw lines / rects / circles  -- HUD overlays
 *
 * We map that onto a libdragon 320x240 16-bit (RGBA5551) double-buffered
 * display.  RGB565 is converted to RGBA5551 on copy.
 */
#include <libdragon.h>
#include <SDL.h>

#undef true
#undef false
#undef bool

#include "DoomRPG.h"
#include "Game.h"
#include "SDL_Video.h"
#include "pd_sound.h"

/* ----------------------------------------------------------- port globals */
SDLVideo_t      sdlVideo;
SDLController_t sdlController;
FluidSynth_t    fluidSynth;

SDLVidModes_t sdlVideoModes[14] = {
    {128, 128}, {128, 160}, {160, 128}, {176, 208}, {176, 220}, {220, 176},
    {240, 320}, {320, 200}, {320, 240}, {352, 416}, {416, 352}, {640, 360},
    {640, 480}, {800, 600}
};

#define SCREEN_W 320
#define SCREEN_H 240

/* A non-NULL token so the game's `if (sdlVideo.renderer)` checks pass. */
#define RENDERER_TOKEN ((SDL_Renderer *)0x1)

/* ------------------------------------------------------------- texture obj */
struct SDL_Texture {
    int       w, h;
    uint16_t *pixels;      /* owned RGB565 copy (image textures) */
    const uint16_t *ext;   /* borrowed RGB565 pixels (streaming) */
    int       ext_pitch;   /* bytes */
    int       streaming;
    int       has_colorkey;
    uint16_t  colorkey565;
};

/* --------------------------------------------------------------- fb state */
static surface_t *s_fb;              /* frame currently being drawn, or NULL */
static SDL_Rect   s_clip;            /* active clip rect (screen space) */
static int        s_clip_on;
static uint16_t   s_draw_color;      /* RGBA5551, for line/rect draws */
static int        s_initialized;

/* 565 -> 5551 (opaque). */
static inline uint16_t cvt565(uint16_t p)
{
    return (uint16_t)((p & 0xF800) | (p & 0x07C0) | ((p & 0x001F) << 1) | 1);
}

/* Convert a run of `n` RGB565 pixels to RGBA5551, two per 32-bit word where
 * src and dst share 4-byte phase (the full-width 3D-view blit always does).
 * Every field stays inside its 16-bit lane, so this is endianness-agnostic
 * and bit-identical to cvt565() per pixel.  The framebuffer is uncached, so
 * halving the store count is the real win here. */
static void blit565(uint16_t *dst, const uint16_t *src, int n)
{
    if (n <= 0) return;

    if ((((uintptr_t)dst ^ (uintptr_t)src) & 3) == 0) {
        if (((uintptr_t)dst & 3) != 0) {          /* 1 px to reach 4-byte align */
            *dst++ = cvt565(*src++);
            n--;
        }
        uint32_t       *d32 = (uint32_t *)dst;
        const uint32_t *s32 = (const uint32_t *)src;
        int words = n >> 1;
        for (int i = 0; i < words; i++) {
            uint32_t p = s32[i];
            d32[i] = (p & 0xF800F800u) | (p & 0x07C007C0u)
                   | ((p & 0x001F001Fu) << 1) | 0x00010001u;
        }
        dst = (uint16_t *)(d32 + words);
        src = (const uint16_t *)(s32 + words);
        n  &= 1;
    }
    while (n-- > 0)
        *dst++ = cvt565(*src++);
}

static inline uint16_t rgb_to_5551(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r >> 3) << 11) | ((g >> 3) << 6) | ((b >> 3) << 1) | 1);
}

static void ensure_frame(void)
{
    if (!s_fb) {
        s_fb = display_get();
    }
}

static inline uint16_t *fb_row(int y)
{
    return (uint16_t *)((uint8_t *)s_fb->buffer + (size_t)y * s_fb->stride);
}

static void clip_bounds(int *x0, int *y0, int *x1, int *y1)
{
    int cx0 = 0, cy0 = 0, cx1 = SCREEN_W, cy1 = SCREEN_H;
    if (s_clip_on) {
        if (s_clip.x > cx0) cx0 = s_clip.x;
        if (s_clip.y > cy0) cy0 = s_clip.y;
        if (s_clip.x + s_clip.w < cx1) cx1 = s_clip.x + s_clip.w;
        if (s_clip.y + s_clip.h < cy1) cy1 = s_clip.y + s_clip.h;
    }
    if (*x0 < cx0) *x0 = cx0;
    if (*y0 < cy0) *y0 = cy0;
    if (*x1 > cx1) *x1 = cx1;
    if (*y1 > cy1) *y1 = cy1;
}

/* ============================================================ init / teardown */
void SDL_InitVideo(void)
{
    SDL_memset(&sdlVideo, 0, sizeof(sdlVideo));
    SDL_memset(&sdlController, 0, sizeof(sdlController));

    sdlVideo.fullScreen      = true;
    sdlVideo.vSync           = true;
    sdlVideo.integerScaling  = true;
    sdlVideo.resolutionIndex = 8;            /* 320x240 */
    sdlVideo.displaySoftKeys = false;
    sdlController.deadZoneLeft  = 25;
    sdlController.deadZoneRight = 25;
    sdlController.padDigitEntry = true;   /* spinner-based code entry (no keyboard) */

    /* Pull any persisted settings (no-op if there is no Config file yet). */
    Game_loadConfig(NULL);

    /* The N64 VI only really wants the standard modes; force 320x240.
     * libdragon requires FILTERS_RESAMPLE (not FILTERS_DISABLED) for any
     * width <= 320 -- the VI cannot scan out a 320-wide buffer unfiltered. */
    display_init(RESOLUTION_320x240, DEPTH_16_BPP, 2, GAMMA_NONE, FILTERS_RESAMPLE);

    sdlVideo.renderer  = RENDERER_TOKEN;
    sdlVideo.rendererW = SCREEN_W;
    sdlVideo.rendererH = SCREEN_H;
    s_draw_color = rgb_to_5551(0, 0, 0);
    s_initialized = 1;

    /* Black out every hw buffer so nothing flashes RDRAM garbage before the
     * game's first frame. */
    for (int i = 0; i < 3; i++) {
        surface_t *fb = display_get();
        for (int y = 0; y < SCREEN_H; y++)
            SDL_memset((uint8_t *)fb->buffer + (size_t)y * fb->stride, 0, SCREEN_W * 2);
        display_show(fb);
    }
}

void SDL_Close(void)
{
    if (s_fb) { display_show(s_fb); s_fb = NULL; }
    if (s_initialized) { display_close(); s_initialized = 0; }
}

/* ------------------------------------------------------------ fatal message
 * DoomRPG_Error() prints the reason to stdout (isviewer/usblog only) and then
 * tears the display down and exit(0)s -- on hardware that just leaves a black
 * screen with no clue what went wrong.  Route it here instead: paint the
 * message on the N64 screen and stop, so the failing step is visible without
 * a USB capture rig.  Never returns.
 */
void PD_FatalMessage(const char *title, const char *msg)
{
    if (!s_initialized) {
        display_init(RESOLUTION_320x240, DEPTH_16_BPP, 2, GAMMA_NONE, FILTERS_RESAMPLE);
        s_initialized = 1;
    }

    surface_t *fb = s_fb ? s_fb : display_get();
    s_fb = NULL;

    graphics_fill_screen(fb, graphics_make_color(0, 0, 40, 255));
    graphics_set_color(graphics_make_color(255, 80, 80, 255), 0);
    graphics_draw_text(fb, 8, 8, title ? title : "FATAL ERROR");
    graphics_set_color(graphics_make_color(255, 255, 255, 255), 0);

    /* crude word-wrap: the built-in font is 8px wide, ~40 cols across 320px */
    if (msg) {
        char line[41];
        int col = 0, y = 28;
        for (const char *p = msg; *p; p++) {
            if (*p == '\n' || col == 40) {
                line[col] = 0;
                graphics_draw_text(fb, 8, y, line);
                y += 10; col = 0;
                if (*p == '\n') continue;
            }
            line[col++] = *p;
        }
        if (col) { line[col] = 0; graphics_draw_text(fb, 8, y, line); }
    }

    display_show(fb);
    while (1) { /* halt with the message on screen */ }
}

SDLVideo_t *SDL_GetVideo(void) { return &sdlVideo; }

/* Audio: FluidSynth_t stays zeroed (Sound.c keeps the struct but the N64
 * build never touches the synth); real playback is libdragon mixer + wav64. */
void SDL_InitAudio(void)  { SDL_memset(&fluidSynth, 0, sizeof(fluidSynth)); PD_SoundInit(); }
void SDL_CloseAudio(void) { PD_SoundShutdown(); }

/* ============================================================ textures */
SDL_Texture *SDL_CreateTexture(SDL_Renderer *r, Uint32 fmt, int access, int w, int h)
{
    (void)r; (void)fmt; (void)access;
    SDL_Texture *t = (SDL_Texture *)SDL_calloc(1, sizeof(*t));
    t->w = w; t->h = h; t->streaming = 1;
    return t;
}

SDL_Texture *SDL_CreateTextureFromSurface(SDL_Renderer *r, SDL_Surface *s)
{
    (void)r;
    SDL_Texture *t = (SDL_Texture *)SDL_calloc(1, sizeof(*t));
    t->w = s->w; t->h = s->h;
    t->pixels = (uint16_t *)SDL_malloc((size_t)s->w * s->h * 2);

    const SDL_PixelFormat *fmt = s->format;
    for (int y = 0; y < s->h; y++) {
        const uint8_t *srow = (const uint8_t *)s->pixels + (size_t)y * s->pitch;
        uint16_t *drow = t->pixels + (size_t)y * s->w;
        for (int x = 0; x < s->w; x++) {
            uint8_t rr, gg, bb;
            if (fmt->palette) {
                SDL_Color c = fmt->palette->colors[srow[x]];
                rr = c.r; gg = c.g; bb = c.b;
            } else {
                const uint8_t *px = srow + (size_t)x * fmt->BytesPerPixel;
                bb = px[0]; gg = px[1]; rr = px[2];
            }
            uint16_t p565 = (uint16_t)(((rr >> 3) << 11) | ((gg >> 2) << 5) | (bb >> 3));
            drow[x] = p565;
        }
    }

    if (s->has_colorkey) {
        t->has_colorkey = 1;
        /* colorkey stored by SDL_MapRGB as a packed RGB565 value */
        t->colorkey565 = (uint16_t)s->colorkey;
    }
    return t;
}

void SDL_DestroyTexture(SDL_Texture *t)
{
    if (!t) return;
    SDL_free(t->pixels);
    SDL_free(t);
}

/* The engine renders into an *uncached* framebuffer (PD_FbAlloc): column-major
 * wall/sprite spans thrash the dcache with write-allocate misses, and uncached
 * stores skip that.  The blit still wants to *read* it cached (sequential), so
 * here we take the cached alias of that buffer and drop any stale lines left
 * by the previous frame's blit -- after which blit565() reads it fast.
 * Guarded on the KSEG1 range so a revert to a cached framebuffer is safe. */
int SDL_UpdateTexture(SDL_Texture *t, const SDL_Rect *rect, const void *pixels, int pitch)
{
    (void)rect;
    if (!t) return -1;

    unsigned long p = (unsigned long)pixels;
    if (p >= 0xA0000000ul && p < 0xC0000000ul) {
        /* Drain the CPU write buffer: an uncached load is ordered after all
         * prior uncached stores, so the renderer's span writes are guaranteed
         * in RDRAM before the blit takes its cached view below. */
        volatile uint16_t sink = *((volatile const uint16_t *)pixels +
                                   ((unsigned long)pitch / 2) * (t->h - 1));
        (void)sink;

        void *cached = CachedAddr(pixels);
        data_cache_hit_invalidate(cached, (unsigned long)pitch * (unsigned long)t->h);
        t->ext = (const uint16_t *)cached;
    } else {
        t->ext = (const uint16_t *)pixels;
    }
    t->ext_pitch = pitch;
    return 0;
}

/* Framebuffer the software renderer draws into -- uncached (see above). */
void *PD_FbAlloc(unsigned long size)
{
    void *p = malloc_uncached(size);
    if (p) memset(p, 0, size);
    return p;
}

void PD_FbFree(void *p)
{
    if (p) free_uncached(p);
}

/* ============================================================ render copy */
int SDL_RenderCopy(SDL_Renderer *r, SDL_Texture *t, const SDL_Rect *src, const SDL_Rect *dst)
{
    (void)r;
    if (!t) return -1;
    ensure_frame();

    SDL_Rect s = { 0, 0, t->w, t->h };
    if (src) s = *src;

    /* DoomRPG's tiled blits (Hud_drawBarTiles, backgrounds) pass a source
     * rect whose width/height is the *remaining area to fill*, much larger
     * than the texture, and rely on the blitter clamping it to the texture
     * -- real SDL does.  Without this clamp the scale math maps 20 dst
     * pixels across a 300px "source", sampling only every 15th column and
     * leaving vertical stripes of stale pixels between them. */
    if (s.x < 0)            { s.w += s.x; s.x = 0; }
    if (s.y < 0)            { s.h += s.y; s.y = 0; }
    if (s.x + s.w > t->w)   s.w = t->w - s.x;
    if (s.y + s.h > t->h)   s.h = t->h - s.y;

    SDL_Rect d = { 0, 0, s.w, s.h };
    if (dst) d = *dst;
    if (d.w <= 0 || d.h <= 0 || s.w <= 0 || s.h <= 0) return 0;

    int dx0 = d.x, dy0 = d.y, dx1 = d.x + d.w, dy1 = d.y + d.h;
    clip_bounds(&dx0, &dy0, &dx1, &dy1);

    const int      src_is_ext = (t->streaming && t->ext);
    const uint16_t *sp        = src_is_ext ? t->ext : t->pixels;
    const int      spitch_px  = src_is_ext ? (t->ext_pitch / 2) : t->w;
    if (!sp) return 0;

    /* Fast path: 1:1 unscaled blit (the 3D view present and most HUD tiles).
     * Skips the per-pixel 64-bit multiply/divide the general scaler needs --
     * that math dominated the frame on the VR4300. */
    if (s.w == d.w && s.h == d.h) {
        const int ck  = (!src_is_ext && t->has_colorkey);
        const uint16_t key = t->colorkey565;
        const int soff = s.x - d.x;
        for (int y = dy0; y < dy1; y++) {
            int sy = s.y + (y - d.y);
            if (sy < 0 || sy >= t->h) continue;
            const uint16_t *srow = sp + (size_t)sy * spitch_px + soff;
            uint16_t *drow = fb_row(y);

            /* clamp the row run to valid source columns (dx0..dx1 is already
             * clamped to the framebuffer by clip_bounds) */
            int rx0 = dx0, rx1 = dx1;
            if (soff + rx0 < 0)     rx0 = -soff;
            if (soff + rx1 > t->w)  rx1 = t->w - soff;
            if (rx1 <= rx0) continue;

            if (!ck) {
                blit565(drow + rx0, srow + rx0, rx1 - rx0);
            } else {
                for (int x = rx0; x < rx1; x++) {
                    uint16_t px = srow[x];
                    if (px == key) continue;
                    drow[x] = cvt565(px);
                }
            }
        }
        return 0;
    }

    for (int y = dy0; y < dy1; y++) {
        int sy = s.y + (int)((int64_t)(y - d.y) * s.h / d.h);
        if (sy < 0 || sy >= t->h) continue;
        const uint16_t *srow = sp + (size_t)sy * spitch_px;
        uint16_t *drow = fb_row(y);
        for (int x = dx0; x < dx1; x++) {
            int sx = s.x + (int)((int64_t)(x - d.x) * s.w / d.w);
            if (sx < 0 || sx >= t->w) continue;
            uint16_t px = srow[sx];
            if (src_is_ext) {
                drow[x] = cvt565(px);
            } else {
                if (t->has_colorkey && px == t->colorkey565) continue;
                drow[x] = cvt565(px);
            }
        }
    }
    return 0;
}

void SDL_RenderPresent(SDL_Renderer *r)
{
    (void)r;
    if (s_fb) { display_show(s_fb); s_fb = NULL; }
}

int SDL_RenderClear(SDL_Renderer *r)
{
    (void)r;
    ensure_frame();
    const uint32_t pair = ((uint32_t)s_draw_color << 16) | s_draw_color;
    for (int y = 0; y < SCREEN_H; y++) {
        uint32_t *row = (uint32_t *)fb_row(y);
        for (int x = 0; x < SCREEN_W / 2; x++) row[x] = pair;
    }
    return 0;
}

int SDL_RenderSetClipRect(SDL_Renderer *r, const SDL_Rect *rect)
{
    (void)r;
    if (rect) { s_clip = *rect; s_clip_on = 1; }
    else        s_clip_on = 0;
    return 0;
}

int SDL_SetRenderDrawColor(SDL_Renderer *r, Uint8 red, Uint8 g, Uint8 b, Uint8 a)
{
    (void)r; (void)a;
    s_draw_color = rgb_to_5551(red, g, b);
    return 0;
}

int SDL_SetRenderDrawBlendMode(SDL_Renderer *r, int m) { (void)r; (void)m; return 0; }
int SDL_SetTextureBlendMode(SDL_Texture *t, int m)     { (void)t; (void)m; return 0; }
int SDL_SetTextureColorMod(SDL_Texture *t, Uint8 r, Uint8 g, Uint8 b) { (void)t;(void)r;(void)g;(void)b; return 0; }
int SDL_SetTextureAlphaMod(SDL_Texture *t, Uint8 a)    { (void)t; (void)a; return 0; }

static void put_px(int x, int y)
{
    if (x < 0 || y < 0 || x >= SCREEN_W || y >= SCREEN_H) return;
    if (s_clip_on &&
        (x < s_clip.x || y < s_clip.y ||
         x >= s_clip.x + s_clip.w || y >= s_clip.y + s_clip.h)) return;
    fb_row(y)[x] = s_draw_color;
}

int SDL_RenderDrawPoint(SDL_Renderer *r, int x, int y) { (void)r; ensure_frame(); put_px(x, y); return 0; }

int SDL_RenderDrawPoints(SDL_Renderer *r, const SDL_Point *pts, int n)
{
    (void)r; ensure_frame();
    for (int i = 0; i < n; i++) put_px(pts[i].x, pts[i].y);
    return 0;
}

int SDL_RenderDrawLine(SDL_Renderer *r, int x1, int y1, int x2, int y2)
{
    (void)r; ensure_frame();
    int dx = abs(x2 - x1), sx = x1 < x2 ? 1 : -1;
    int dy = -abs(y2 - y1), sy = y1 < y2 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        put_px(x1, y1);
        if (x1 == x2 && y1 == y2) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x1 += sx; }
        if (e2 <= dx) { err += dx; y1 += sy; }
    }
    return 0;
}

int SDL_RenderDrawRect(SDL_Renderer *r, const SDL_Rect *rect)
{
    (void)r; ensure_frame();
    if (!rect) return 0;
    int x0 = rect->x, y0 = rect->y, x1 = rect->x + rect->w - 1, y1 = rect->y + rect->h - 1;
    for (int x = x0; x <= x1; x++) { put_px(x, y0); put_px(x, y1); }
    for (int y = y0; y <= y1; y++) { put_px(x0, y); put_px(x1, y); }
    return 0;
}

int SDL_RenderFillRect(SDL_Renderer *r, const SDL_Rect *rect)
{
    (void)r; ensure_frame();
    int x0, y0, x1, y1;
    if (rect) { x0 = rect->x; y0 = rect->y; x1 = rect->x + rect->w; y1 = rect->y + rect->h; }
    else      { x0 = 0; y0 = 0; x1 = SCREEN_W; y1 = SCREEN_H; }
    clip_bounds(&x0, &y0, &x1, &y1);
    for (int y = y0; y < y1; y++) {
        uint16_t *row = fb_row(y);
        for (int x = x0; x < x1; x++) row[x] = s_draw_color;
    }
    return 0;
}

/* Circle helpers -- lifted from the original SDL_Video.c, routed through our
 * own line/point primitives. */
void SDL_RenderDrawFillCircle(SDL_Renderer *renderer, int x, int y, int r)
{
    int dx = r, dy = 0, accum = dx - 1;
    while (dy <= dx) {
        SDL_RenderDrawLine(renderer,  dx + x,  dy + y, -dx + x,  dy + y);
        SDL_RenderDrawLine(renderer,  dy + x,  dx + y, -dy + x,  dx + y);
        SDL_RenderDrawLine(renderer, -dx + x, -dy + y,  dx + x, -dy + y);
        SDL_RenderDrawLine(renderer, -dy + x, -dx + y,  dy + x, -dx + y);
        dy++;
        if ((accum -= (dy << 1) - 1) < 0) { dx--; accum += dx << 1; }
    }
}

void SDL_RenderDrawCircle(SDL_Renderer *renderer, int x, int y, int r)
{
    int dx = r, dy = 0, accum = dx - 1;
    while (dy <= dx) {
        const SDL_Point p[8] = {
            { dx + x,  dy + y}, {-dx + x,  dy + y}, { dy + x,  dx + y}, {-dy + x,  dx + y},
            {-dx + x, -dy + y}, { dx + x, -dy + y}, {-dy + x, -dx + y}, { dy + x, -dx + y}
        };
        SDL_RenderDrawPoints(renderer, p, 8);
        dy++;
        if ((accum -= (dy << 1) - 1) < 0) { dx--; accum += dx << 1; }
    }
}

/* ============================================================ controller-bind
 * menu helpers -- not wired up for milestone 1. */
int   SDL_GameControllerGetButtonID(void)          { return CONTROLLER_BUTTON_INVALID; }
char *SDL_GameControllerGetNameButton(int id)      { (void)id; return ""; }
char *SDL_MouseGetNameButton(int id)               { (void)id; return ""; }
int   SDL_JoystickGetButtonID(void)                { return CONTROLLER_BUTTON_INVALID; }
