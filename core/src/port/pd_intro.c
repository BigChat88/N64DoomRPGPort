/*
 * pd_intro.c  --  "made with libdragon" splash shown before the game.
 *
 * The logo (libdragon/website/logo2.png) is packed into the ROM's DragonFS
 * image as an RGBA16 sprite by the Makefile.  DFS must already be mounted and
 * the display initialised (SDL_InitVideo) before PD_PlayIntro() runs.
 *
 * Static image, held for a moment -- no animation.
 */
#include <libdragon.h>

#define SCREEN_W 320
#define SCREEN_H 240

void PD_PlayIntro(void)
{
    /* asset_load() asserts (crashes) on a missing rom:/ file rather than
     * returning NULL, so probe with dfs_open() first -- the logo sprite is
     * optional (the packer omits it when core is built with `make engine`
     * only, without `libdragon make`'s mksprite step). */
    int fd = dfs_open("/libdragon_logo.sprite");
    if (fd < 0) {
        debugf("[pd_intro] no libdragon_logo.sprite -- skipping splash\n");
        return;
    }
    dfs_close(fd);

    sprite_t *logo = sprite_load("rom:/libdragon_logo.sprite");
    debugf("[pd_intro] sprite_load -> %p\n", logo);
    if (!logo)
        return;

    const int lx = (SCREEN_W - logo->width)  / 2;
    const int ly = (SCREEN_H - logo->height) / 2;

    /* ~1.6s at 60fps; two buffers, so redraw the same frame each vsync */
    for (int i = 0; i < 96; i++) {
        surface_t *fb = display_get();
        graphics_fill_screen(fb, graphics_make_color(0, 0, 0, 255));
        graphics_draw_sprite_trans(fb, lx, ly, logo);
        display_show(fb);
    }

    sprite_free(logo);

    surface_t *fb = display_get();
    graphics_fill_screen(fb, graphics_make_color(0, 0, 0, 255));
    display_show(fb);
}
