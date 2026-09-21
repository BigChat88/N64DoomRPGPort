/*
 * pd_intro.c  --  "made with libdragon" dragon-logo intro, played before the
 * game boots.
 *
 * Ported from 64Doom's src/i_intro.c (I_DragonIntro), which in turn adapted
 * lambertjamesd/n64brew2025's src/intro/logo.c (logo_libdragon) -- see that
 * file's header for the original attribution/license (MIT).
 *
 * This runs at the game's own display resolution (320x240) instead of the
 * original 640x480, so there's no visible resize/flash when the intro hands
 * off to SDL_InitVideo().  The animation was laid out in 640x480 (4:3)
 * design-space coordinates; 320x240 is the same 4:3 aspect, so scaling by
 * height (INTRO_SCALE) exactly matches the width too -- nothing is cropped
 * and INTRO_X_MARGIN comes out to 0.  (64Doom's own port of this same intro
 * runs at Doom's 320x200, which is wider than 4:3, so there INTRO_X_MARGIN
 * is nonzero and the result is centered with side margins instead -- the
 * SX/SY macros below are ported from there unchanged, so this project gets
 * the same margin-instead-of-crop behaviour for free if SCREEN_W/H ever stop
 * matching 640x480's aspect ratio.)  display_init() is still fully torn down
 * (display_close()) before returning, so this must run once, early, before
 * SDL_InitVideo() brings up the game's own display -- see main.c.
 *
 * Like the original, this is silent: the N64 port drives the audio hardware
 * directly with its own mixer (see pd_sound.c) and never touches libdragon's
 * mixer/wav64 subsystem for anything else, so pulling it in for just the
 * dragon roar risked the two audio paths fighting over the AI hardware.
 */
#include <libdragon.h>

#define SCREEN_W 320
#define SCREEN_H 240

/* The animation below was authored in 640x480 design-space coordinates.
 * Scaling by height (rather than width) means nothing is cropped even if
 * SCREEN_W/H's aspect ratio is wider than 640x480's 4:3 -- the scaled design
 * would then be narrower than the screen, so it gets letterboxed (centered
 * with margins) horizontally instead of stretched or cut off. SX/SY map a
 * design-space coordinate to a screen-space one; SXF/SYF are the same as
 * floats, for vertex arrays. */
#define INTRO_SCALE ((float)SCREEN_H / 480.0f)
#define INTRO_X_MARGIN (((float)SCREEN_W - 640.0f*INTRO_SCALE) / 2.0f)
#define SXF(v) ((v)*INTRO_SCALE + INTRO_X_MARGIN)
#define SYF(v) ((v)*INTRO_SCALE)
#define SX(v) ((int)SXF(v))
#define SY(v) ((int)SYF(v))

void PD_PlayIntro(void)
{
    /* The intro sprites are optional (the packer omits them when core is
     * built with `make engine` only, without `libdragon make`'s mksprite
     * step) -- probe before sprite_load(), which asserts on a missing file. */
    static const char *const paths[4] = {
        "/intro/dragon1.sprite", "/intro/dragon2.sprite",
        "/intro/dragon3.sprite", "/intro/dragon4.sprite",
    };
    for (int i = 0; i < 4; i++) {
        int fd = dfs_open(paths[i]);
        if (fd < 0) {
            debugf("[pd_intro] no %s -- skipping intro\n", paths[i]);
            return;
        }
        dfs_close(fd);
    }

    const color_t RED = RGBA32(221, 46, 26, 255);
    const color_t WHITE = RGBA32(255, 255, 255, 255);

    sprite_t *d1 = sprite_load("rom:/intro/dragon1.sprite");
    sprite_t *d2 = sprite_load("rom:/intro/dragon2.sprite");
    sprite_t *d3 = sprite_load("rom:/intro/dragon3.sprite");
    sprite_t *d4 = sprite_load("rom:/intro/dragon4.sprite");

    display_init(RESOLUTION_320x240, DEPTH_16_BPP, 2, GAMMA_NONE, ANTIALIAS_RESAMPLE);

    rdpq_init();

    float angle1=0, angle2=0, angle3=0;
    float scale1=0, scale2=0, scale3=0, scroll4=0;
    uint32_t ms0=0;
    int anim_part=0;
    /* translation offset of the animation (simplify centering) */
    const int X0 = 10, Y0 = 30;

    void reset(void)
    {
        ms0 = get_ticks_ms();
        anim_part = 0;

        angle1 = 3.2f;
        angle2 = 1.9f;
        angle3 = 0.9f;
        scale1 = 0.0f;
        scale2 = 0.4f;
        scale3 = 0.8f;
        scroll4 = 400;
    }

    reset();
    while (1)
    {
        /* Calculate animation part:
         * 0: rotate dragon head
         * 1: rotate dragon body and tail, scale up
         * 2: scroll dragon logo
         * 3: fade out */
        uint32_t tt = get_ticks_ms() - ms0;
        if (tt < 1000) anim_part = 0;
        else if (tt < 1500) anim_part = 1;
        else if (tt < 4000) anim_part = 2;
        else if (tt < 5000) anim_part = 3;
        else break;

        /* Update animation parameters using quadratic ease-out */
        angle1 -= angle1 * 0.04f; if (angle1 < 0.010f) angle1 = 0;
        if (anim_part >= 1)
        {
            angle2 -= angle2 * 0.06f; if (angle2 < 0.01f) angle2 = 0;
            angle3 -= angle3 * 0.06f; if (angle3 < 0.01f) angle3 = 0;
            scale2 -= scale2 * 0.06f; if (scale2 < 0.01f) scale2 = 0;
            scale3 -= scale3 * 0.06f; if (scale3 < 0.01f) scale3 = 0;
        }
        if (anim_part >= 2)
        {
            scroll4 -= scroll4 * 0.08f;
        }

        /* Update colors for fade out effect */
        color_t red = RED;
        color_t white = WHITE;
        if (anim_part >= 3)
        {
            red.a = 255 - (tt-4000) * 255 / 1000;
            white.a = 255 - (tt-4000) * 255 / 1000;
        }

        surface_t *fb = display_get();
        rdpq_attach_clear(fb, NULL);

        /* To simulate the dragon jumping out, we scissor the head so that
         * it appears as it moves. */
        if (angle1 > 1.0f)
        {
            /* Initially, also scissor horizontally,
             * so that the head tail is not visible on the right. */
            rdpq_set_scissor(SX(0), SY(0), SX(X0+300), SY(Y0+240));
        }
        else
        {
            rdpq_set_scissor(SX(0), SY(0), SX(640), SY(Y0+240));
        }

        /* Draw dragon head */
        rdpq_set_mode_standard();
        rdpq_mode_alphacompare(1);
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
        rdpq_mode_combiner(RDPQ_COMBINER1((0,0,0,PRIM),(TEX0,0,PRIM,0)));
        rdpq_set_prim_color(red);
        rdpq_sprite_blit(d1, SX(X0+216), SY(Y0+205), &(rdpq_blitparms_t){
            .theta = angle1, .scale_x = (scale1+1)*INTRO_SCALE, .scale_y = (scale1+1)*INTRO_SCALE,
            .cx = 176, .cy = 171,
        });

        /* Restore scissor to standard (the real screen bounds, not a scaled
         * design-space rect - this call means "stop constraining", so it
         * should just open all the way up, not leave a pointless margin). */
        rdpq_set_scissor(0, 0, SCREEN_W, SCREEN_H);

        /* Draw a black rectangle with alpha gradient, to cover the head tail */
        rdpq_mode_combiner(RDPQ_COMBINER_SHADE);
        rdpq_mode_dithering(DITHER_NOISE_NOISE);
        float vtx[4][6] = {
            //  x,    y,  r,g,b,a
            { SXF(X0+0),   SYF(Y0+180), 0,0,0,0 },
            { SXF(X0+200), SYF(Y0+180), 0,0,0,0 },
            { SXF(X0+200), SYF(Y0+240), 0,0,0,1 },
            { SXF(X0+0),   SYF(Y0+240), 0,0,0,1 },
        };
        rdpq_triangle(&TRIFMT_SHADE, vtx[0], vtx[1], vtx[2]);
        rdpq_triangle(&TRIFMT_SHADE, vtx[0], vtx[2], vtx[3]);

        if (anim_part >= 1)
        {
            /* Draw dragon body and tail */
            rdpq_set_mode_standard();
            rdpq_mode_alphacompare(1);
            rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
            rdpq_mode_combiner(RDPQ_COMBINER1((0,0,0,PRIM),(TEX0,0,PRIM,0)));

            /* Fade them in */
            color_t color = red;
            color.r *= 1-scale3; color.g *= 1-scale3; color.b *= 1-scale3;
            rdpq_set_prim_color(color);

            rdpq_sprite_blit(d2, SX(X0+246), SY(Y0+230), &(rdpq_blitparms_t){
                .theta = angle2, .scale_x = (1-scale2)*INTRO_SCALE, .scale_y = (1-scale2)*INTRO_SCALE,
                .cx = 145, .cy = 113,
            });

            rdpq_sprite_blit(d3, SX(X0+266), SY(Y0+256), &(rdpq_blitparms_t){
                .theta = -angle3, .scale_x = (1-scale3)*INTRO_SCALE, .scale_y = (1-scale3)*INTRO_SCALE,
                .cx = 91, .cy = 24,
            });
        }

        /* Draw scrolling logo */
        if (anim_part >= 2)
        {
            rdpq_set_prim_color(white);
            rdpq_sprite_blit(d4, SX(X0 + 161 + (int)scroll4), SY(Y0 + 182), &(rdpq_blitparms_t){
                .scale_x = INTRO_SCALE, .scale_y = INTRO_SCALE,
            });
        }

        rdpq_detach_show();
    }

    rspq_wait();
    sprite_free(d1);
    sprite_free(d2);
    sprite_free(d3);
    sprite_free(d4);
    display_close();
}
