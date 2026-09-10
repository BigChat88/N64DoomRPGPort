/*
 * pd_bmp.c  --  just enough BMP loading for DoomRPG-RE's image path.
 *
 * The game ships 8-bit palettised (and a few 24-bit) uncompressed Windows
 * BMPs inside DoomRPG.zip.  It calls SDL_LoadBMP_RW on an in-memory copy,
 * optionally sets a magenta colour key, rewrites the palette, then converts
 * the surface to a texture (see pd_video.c: SDL_CreateTextureFromSurface).
 *
 * All multi-byte header fields are little-endian on disk and are assembled
 * byte-wise here so the loader is host-endianness independent.
 */
#include <libdragon.h>
#include <SDL.h>

static Uint16 rd16(const Uint8 *p) { return (Uint16)(p[0] | (p[1] << 8)); }
static Uint32 rd32(const Uint8 *p)
{
    return (Uint32)p[0] | ((Uint32)p[1] << 8) | ((Uint32)p[2] << 16) | ((Uint32)p[3] << 24);
}

Uint32 SDL_MapRGB(const SDL_PixelFormat *fmt, Uint8 r, Uint8 g, Uint8 b)
{
    (void)fmt;
    /* Packed RGB565 -- must match the packing used in pd_video.c. */
    return (Uint32)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

int SDL_SetColorKey(SDL_Surface *surface, int flag, Uint32 key)
{
    if (!surface) return -1;
    surface->has_colorkey = flag ? 1 : 0;
    surface->colorkey = key;
    return 0;
}

void SDL_FreeSurface(SDL_Surface *surface)
{
    if (!surface) return;
    if (surface->format) {
        if (surface->format->palette) {
            SDL_free(surface->format->palette->colors);
            SDL_free(surface->format->palette);
        }
        SDL_free(surface->format);
    }
    SDL_free(surface->pixels);
    SDL_free(surface);
}

SDL_Surface *SDL_LoadBMP_RW(SDL_RWops *src, int freesrc)
{
    SDL_Surface *surf = NULL;
    Uint8  fh[14], ih[40];
    Uint8 *palraw = NULL;
    Uint8 *rowbuf = NULL;
    Uint32 offbits, hdrsize, compress, clrused;
    int    width, heightRaw, topdown, height;
    Uint16 bpp;

    if (!src) return NULL;

    if (SDL_RWread(src, fh, 1, 14) != 14) goto done;
    if (fh[0] != 'B' || fh[1] != 'M')    goto done;
    offbits = rd32(fh + 10);

    if (SDL_RWread(src, ih, 1, 40) != 40) goto done;
    hdrsize   = rd32(ih + 0);
    width     = (int)rd32(ih + 4);
    heightRaw = (int)rd32(ih + 8);
    topdown   = heightRaw < 0;
    height    = topdown ? -heightRaw : heightRaw;
    bpp       = rd16(ih + 14);
    compress  = rd32(ih + 16);   /* biCompression; ih+20 is biSizeImage */
    clrused   = rd32(ih + 32);

    if (compress != 0) { debugf("[pd_bmp] unsupported compression %lu\n", (unsigned long)compress); goto done; }
    if (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 24 && bpp != 32) {
        debugf("[pd_bmp] unsupported bpp %u\n", bpp); goto done;
    }
    if (width <= 0 || height <= 0 || width > 2048 || height > 2048) goto done;

    /* skip any extra header bytes beyond the classic 40-byte BITMAPINFOHEADER */
    if (hdrsize > 40) SDL_RWseek(src, (Sint64)hdrsize - 40, SDL_RWSEEK_CUR);

    surf = (SDL_Surface *)SDL_calloc(1, sizeof(*surf));
    surf->format = (SDL_PixelFormat *)SDL_calloc(1, sizeof(*surf->format));
    surf->w = width;
    surf->h = height;
    surf->refcount = 1;

    if (bpp <= 8) {
        /* 1/4/8-bit palettised.  Whatever the source depth, the surface we
         * hand back is 8-bit indices -- the rest of the pipeline only knows
         * palette + 8bpp (see SDL_CreateTextureFromSurface). */
        int ncolors = (int)(clrused ? clrused : (1u << bpp));
        surf->format->BitsPerPixel  = 8;
        surf->format->BytesPerPixel = 1;
        surf->format->palette = (SDL_Palette *)SDL_calloc(1, sizeof(SDL_Palette));
        surf->format->palette->ncolors = ncolors;
        surf->format->palette->colors  = (SDL_Color *)SDL_calloc(ncolors, sizeof(SDL_Color));

        palraw = (Uint8 *)SDL_malloc((size_t)ncolors * 4);
        if (SDL_RWread(src, palraw, 1, (size_t)ncolors * 4) != (size_t)ncolors * 4) goto fail;
        for (int i = 0; i < ncolors; i++) {
            surf->format->palette->colors[i].b = palraw[i * 4 + 0];
            surf->format->palette->colors[i].g = palraw[i * 4 + 1];
            surf->format->palette->colors[i].r = palraw[i * 4 + 2];
            surf->format->palette->colors[i].a = 255;
        }
        surf->pitch  = width;
        surf->pixels = SDL_malloc((size_t)width * height);
    } else {
        int Bpp = bpp / 8;
        surf->format->BitsPerPixel  = bpp;
        surf->format->BytesPerPixel = (Uint8)Bpp;
        surf->pitch  = width * Bpp;
        surf->pixels = SDL_malloc((size_t)surf->pitch * height);
    }

    SDL_RWseek(src, (Sint64)offbits, SDL_RWSEEK_SET);

    {
        /* bytes per source row (before the 4-byte BMP row padding) */
        int srcrowbytes = (bpp <= 8) ? (int)(((size_t)width * bpp + 7) / 8)
                                     : width * (bpp / 8);
        int padded      = (srcrowbytes + 3) & ~3;
        rowbuf = (Uint8 *)SDL_malloc((size_t)padded);
        for (int y = 0; y < height; y++) {
            int dsty = topdown ? y : (height - 1 - y);
            Uint8 *drow = (Uint8 *)surf->pixels + (size_t)dsty * surf->pitch;

            /* tolerate a missing pad on the final row: only the first
             * srcrowbytes carry pixels */
            if (SDL_RWread(src, rowbuf, 1, (size_t)padded) < (size_t)srcrowbytes)
                goto fail;

            if (bpp == 8 || bpp == 24 || bpp == 32) {
                SDL_memcpy(drow, rowbuf, (size_t)srcrowbytes);
            } else if (bpp == 4) {
                for (int x = 0; x < width; x++)
                    drow[x] = (x & 1) ? (rowbuf[x >> 1] & 0x0F)
                                      : (rowbuf[x >> 1] >> 4);
            } else { /* bpp == 1 */
                for (int x = 0; x < width; x++)
                    drow[x] = (rowbuf[x >> 3] >> (7 - (x & 7))) & 1;
            }
        }
        SDL_free(rowbuf);
        rowbuf = NULL;
    }

    goto done;

fail:
    SDL_free(palraw);
    SDL_FreeSurface(surf);
    surf = NULL;
    palraw = NULL;

done:
    SDL_free(palraw);
    SDL_free(rowbuf);
    if (src && freesrc) SDL_RWclose(src);
    return surf;
}
