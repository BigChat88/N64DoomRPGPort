/*
 * pd_rwops.c  --  SDL_RWops implementation for the N64 port.
 *
 * Read streams are served from the DragonFS image linked into the ROM
 * (assets/ -> rom:/).  Memory streams back the in-RAM .zip entry decoding.
 * Write streams for the four save slots ("Config" / "Player" / "Player2" /
 * "World") are buffered in RAM and committed to cartridge FlashRAM on close
 * (see pd_save.c); reads of those names come straight back out of the store.
 * Any other write target is still accepted and discarded.
 */
#include <libdragon.h>
#include <SDL.h>
#include "pd_save.h"

/* --------------------------------------------------------------- DFS read */
static Sint64 dfs_size_cb(SDL_RWops *ctx)
{
    return (Sint64)dfs_size((uint32_t)ctx->hidden_num);
}

static Sint64 dfs_seek_cb(SDL_RWops *ctx, Sint64 offset, int whence)
{
    int origin = SEEK_SET;
    if (whence == SDL_RWSEEK_CUR) origin = SEEK_CUR;
    else if (whence == SDL_RWSEEK_END) origin = SEEK_END;
    dfs_seek((uint32_t)ctx->hidden_num, (int)offset, origin);
    return (Sint64)dfs_tell((uint32_t)ctx->hidden_num);
}

static size_t dfs_read_cb(SDL_RWops *ctx, void *ptr, size_t size, size_t maxnum)
{
    if (size == 0) return 0;
    int got = dfs_read(ptr, (int)size, (int)maxnum, (uint32_t)ctx->hidden_num);
    return got > 0 ? (size_t)got / size : 0;
}

static size_t dfs_write_cb(SDL_RWops *ctx, const void *ptr, size_t size, size_t num)
{
    (void)ctx; (void)ptr; (void)size; (void)num;
    return 0;   /* read-only medium */
}

static int dfs_close_cb(SDL_RWops *ctx)
{
    dfs_close((uint32_t)ctx->hidden_num);
    SDL_free(ctx);
    return 0;
}

/* --------------------------------------------------------------- memory */
static Sint64 mem_size_cb(SDL_RWops *ctx) { return (Sint64)(ctx->mem.stop - ctx->mem.base); }

static Sint64 mem_seek_cb(SDL_RWops *ctx, Sint64 offset, int whence)
{
    Uint8 *base = ctx->mem.base;
    Uint8 *newpos;
    if (whence == SDL_RWSEEK_SET)      newpos = base + offset;
    else if (whence == SDL_RWSEEK_CUR) newpos = ctx->mem.here + offset;
    else                              newpos = ctx->mem.stop + offset;
    if (newpos < base) newpos = base;
    if (newpos > ctx->mem.stop) newpos = ctx->mem.stop;
    ctx->mem.here = newpos;
    return (Sint64)(newpos - base);
}

static size_t mem_read_cb(SDL_RWops *ctx, void *ptr, size_t size, size_t maxnum)
{
    if (size == 0 || maxnum == 0) return 0;
    size_t avail = (size_t)(ctx->mem.stop - ctx->mem.here) / size;
    if (avail == 0) return 0;
    if (maxnum > avail) maxnum = avail;
    SDL_memcpy(ptr, ctx->mem.here, maxnum * size);
    ctx->mem.here += maxnum * size;
    return maxnum;
}

static size_t mem_write_cb(SDL_RWops *ctx, const void *ptr, size_t size, size_t num)
{
    size_t avail = (size_t)(ctx->mem.stop - ctx->mem.here) / (size ? size : 1);
    if (num > avail) num = avail;
    SDL_memcpy(ctx->mem.here, ptr, num * size);
    ctx->mem.here += num * size;
    return num;
}

static int mem_close_cb(SDL_RWops *ctx) { SDL_free(ctx); return 0; }

/* ----------------------------------------------------------- save streams */
/* Read: a memory stream over a malloc'd copy of the slot, freed on close. */
static int savemem_close_cb(SDL_RWops *ctx)
{
    SDL_free(ctx->hidden_ptr);
    SDL_free(ctx);
    return 0;
}

/* Write: append into a growable RAM buffer (mem.base..mem.stop is the
 * allocation, mem.here the write cursor); commit to the store on close. */
static Sint64 savew_size_cb(SDL_RWops *ctx)
{
    return (Sint64)(ctx->mem.here - ctx->mem.base);
}

static Sint64 savew_seek_cb(SDL_RWops *ctx, Sint64 offset, int whence)
{
    Uint8 *base = ctx->mem.base, *end = ctx->mem.here, *pos;
    if (whence == SDL_RWSEEK_SET)      pos = base + offset;
    else if (whence == SDL_RWSEEK_CUR) pos = ctx->mem.here + offset;
    else                               pos = end + offset;
    if (pos < base) pos = base;
    if (pos > end)  pos = end;          /* cannot seek past what was written */
    ctx->mem.here = pos;
    return (Sint64)(pos - base);
}

static size_t savew_read_cb(SDL_RWops *ctx, void *ptr, size_t sz, size_t n)
{
    (void)ctx; (void)ptr; (void)sz; (void)n;
    return 0;
}

static size_t savew_write_cb(SDL_RWops *ctx, const void *ptr, size_t sz, size_t n)
{
    size_t need = sz * n;
    if (need == 0) return 0;

    if (ctx->mem.here + need > ctx->mem.stop) {
        size_t used = (size_t)(ctx->mem.here - ctx->mem.base);
        size_t cap  = (size_t)(ctx->mem.stop - ctx->mem.base);
        size_t want = cap ? cap : 4096;
        while (want < used + need) want *= 2;
        Uint8 *nb = (Uint8 *)SDL_realloc(ctx->mem.base, want);
        if (!nb) return 0;
        ctx->mem.base = nb;
        ctx->mem.here = nb + used;
        ctx->mem.stop = nb + want;
    }

    SDL_memcpy(ctx->mem.here, ptr, need);
    ctx->mem.here += need;
    return n;
}

static int savew_close_cb(SDL_RWops *ctx)
{
    PD_SaveWrite((const char *)ctx->hidden_ptr, ctx->mem.base,
                 (int)(ctx->mem.here - ctx->mem.base));
    SDL_free(ctx->mem.base);
    SDL_free(ctx->hidden_ptr);
    SDL_free(ctx);
    return 0;
}

/* --------------------------------------------------------------- null sink */
static Sint64 null_size_cb(SDL_RWops *c) { (void)c; return 0; }
static Sint64 null_seek_cb(SDL_RWops *c, Sint64 o, int w) { (void)c;(void)o;(void)w; return 0; }
static size_t null_rw_cb(SDL_RWops *c, void *p, size_t s, size_t n) { (void)c;(void)p;(void)s; return n; }
static size_t null_cw_cb(SDL_RWops *c, const void *p, size_t s, size_t n) { (void)c;(void)p;(void)s; return n; }
static int    null_close_cb(SDL_RWops *c) { SDL_free(c); return 0; }

/* --------------------------------------------------------------- factories */
SDL_RWops *SDL_RWFromFile(const char *file, const char *mode)
{
    if (!file || !mode) return NULL;

    const char *slot = PD_SaveCanon(file);

    if (mode[0] == 'w' || mode[0] == 'a') {
        if (slot) {
            SDL_RWops *rw = (SDL_RWops *)SDL_calloc(1, sizeof(*rw));
            rw->size = savew_size_cb; rw->seek = savew_seek_cb;
            rw->read = savew_read_cb; rw->write = savew_write_cb;
            rw->close = savew_close_cb;
            rw->hidden_ptr = SDL_strdup(slot);
            return rw;   /* mem.base/here/stop start NULL: first write allocs */
        }
        SDL_RWops *rw = (SDL_RWops *)SDL_calloc(1, sizeof(*rw));
        rw->size = null_size_cb; rw->seek = null_seek_cb;
        rw->read = null_rw_cb;   rw->write = null_cw_cb; rw->close = null_close_cb;
        return rw;
    }

    if (slot) {
        void *buf = NULL;
        int   len = 0;
        if (!PD_SaveRead(slot, &buf, &len))
            return NULL;   /* no save yet -> caller sees "no saved game" */
        SDL_RWops *rw = (SDL_RWops *)SDL_calloc(1, sizeof(*rw));
        rw->size = mem_size_cb; rw->seek = mem_seek_cb;
        rw->read = mem_read_cb; rw->write = mem_write_cb;
        rw->close = savemem_close_cb;
        rw->mem.base = (Uint8 *)buf;
        rw->mem.here = (Uint8 *)buf;
        rw->mem.stop = (Uint8 *)buf + len;
        rw->hidden_ptr = buf;
        return rw;
    }

    char path[128];
    if (file[0] == '/') snprintf(path, sizeof(path), "%s", file);
    else                snprintf(path, sizeof(path), "/%s", file);

    int h = dfs_open(path);
    if (h < 0) {
        debugf("[pd_rwops] dfs_open(%s) failed: %d\n", path, h);
        return NULL;
    }

    SDL_RWops *rw = (SDL_RWops *)SDL_calloc(1, sizeof(*rw));
    rw->size = dfs_size_cb; rw->seek = dfs_seek_cb;
    rw->read = dfs_read_cb; rw->write = dfs_write_cb; rw->close = dfs_close_cb;
    rw->hidden_num = h;
    return rw;
}

static SDL_RWops *rw_from_mem(void *mem, int size)
{
    SDL_RWops *rw = (SDL_RWops *)SDL_calloc(1, sizeof(*rw));
    rw->size = mem_size_cb; rw->seek = mem_seek_cb;
    rw->read = mem_read_cb; rw->write = mem_write_cb; rw->close = mem_close_cb;
    rw->mem.base = (Uint8 *)mem;
    rw->mem.here = (Uint8 *)mem;
    rw->mem.stop = (Uint8 *)mem + size;
    return rw;
}

SDL_RWops *SDL_RWFromMem(void *mem, int size)            { return rw_from_mem(mem, size); }
SDL_RWops *SDL_RWFromConstMem(const void *mem, int size) { return rw_from_mem((void *)mem, size); }
