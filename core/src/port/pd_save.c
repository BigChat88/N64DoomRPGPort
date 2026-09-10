/*
 * pd_save.c  --  cartridge SRAM save store for the N64 port.
 *
 * Two layers:
 *
 *   1. A flat 32 KiB battery-backed SRAM window at 0x08000000, read and
 *      written with PI DMA (PI domain 2 bus timing).  Unlike FlashRAM there
 *      is no command protocol -- the flashcart / emulator maps it as plain
 *      memory.  The port started on FlashRAM but the MX29L1100-style write
 *      sequence was not honoured by the EverDrive-64 X7 FlashRAM emulation
 *      (the .fla was created but stayed empty), so it now uses SRAM, the
 *      same save class that works reliably for EEPROM titles on the cart.
 *
 *   2. A fixed-directory store on top of a RAM shadow of that window.  Up to
 *      eight named slots; the game only ever uses four ("Config", "Player",
 *      "Player2", "World").  Reads are served from the shadow; a write
 *      rebuilds the shadow and rewrites the whole window, then reads it back
 *      to confirm the save actually stuck.
 *
 * The ROM must declare  N64_ROM_SAVETYPE = sram256k  (see Makefile) so the
 * flashcart / emulator provides a persistent 32 KiB SRAM save.  With no save
 * memory present the store still works for the current session and warns.
 */
#include <libdragon.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "pd_save.h"

/* --------------------------------------------------------------- SRAM driver */

#define SRAM_ADDR    0x08000000u
#define SRAM_SIZE    32768u          /* 256 kbit -- the universally-mapped size */

/* PI domain 2 (SRAM/FlashRAM) bus-timing registers. */
#define PI_STATUS_REG    (*(volatile uint32_t *)0xA4600010)
#define PI_DOM2_LAT_REG  (*(volatile uint32_t *)0xA4600024)
#define PI_DOM2_PWD_REG  (*(volatile uint32_t *)0xA4600028)
#define PI_DOM2_PGS_REG  (*(volatile uint32_t *)0xA460002C)
#define PI_DOM2_RLS_REG  (*(volatile uint32_t *)0xA4600030)

/* Save-memory availability.  UNKNOWN means nothing has been written yet; the
 * first flush reads its own write back and settles this to PRESENT or ABSENT
 * so a cart with no SRAM does not silently turn every save into a
 * session-only one, and one slow failed write is not repeated forever. */
enum { SAVE_ABSENT = -1, SAVE_UNKNOWN = 0, SAVE_PRESENT = 1 };
static int g_save_state = SAVE_UNKNOWN;

/* DMA endpoints must be 8-byte aligned; 16 keeps whole cache lines clear. */
static uint8_t g_shadow[SRAM_SIZE]  __attribute__((aligned(16)));
static uint8_t g_verify[SRAM_SIZE]  __attribute__((aligned(16)));

static void sram_bus_timing(void)
{
    while (PI_STATUS_REG & 3) { /* wait for PI idle */ }
    PI_DOM2_LAT_REG = 0x05;
    PI_DOM2_PWD_REG = 0x0C;
    PI_DOM2_PGS_REG = 0x0D;
    PI_DOM2_RLS_REG = 0x02;
}

static void sram_read(uint32_t off, void *dst, uint32_t len)
{
    data_cache_hit_writeback_invalidate(dst, len);
    dma_read_raw_async(dst, SRAM_ADDR + off, len);
    dma_wait();
    data_cache_hit_invalidate(dst, len);
}

static void sram_write(uint32_t off, const void *src, uint32_t len)
{
    data_cache_hit_writeback_invalidate((void *)src, len);
    dma_write_raw_async(src, SRAM_ADDR + off, len);
    dma_wait();
}

/* ------------------------------------------------------------- save store */

#define STORE_MAGIC     "DRPGSAV1"
#define STORE_NSLOTS    8
#define STORE_DIR_SIZE  256
#define STORE_DATA_OFF  STORE_DIR_SIZE
#define STORE_DATA_CAP  (SRAM_SIZE - STORE_DIR_SIZE)

typedef struct {
    char     name[16];   /* nul-padded; name[0] == 0 marks a free slot */
    uint32_t off;        /* byte offset into the data region            */
    uint32_t len;
    uint32_t rsvd;
} store_slot_t;          /* 28 bytes, no padding */

typedef struct {
    char         magic[8];
    uint32_t     nslots;
    uint32_t     rsvd;
    store_slot_t slots[STORE_NSLOTS];
} store_dir_t;           /* 240 bytes, fits STORE_DIR_SIZE */

static store_dir_t *store_dir(void)  { return (store_dir_t *)g_shadow; }
static uint8_t     *store_data(void) { return g_shadow + STORE_DATA_OFF; }

static void store_format(void)
{
    memset(g_shadow, 0, STORE_DIR_SIZE);
    memcpy(store_dir()->magic, STORE_MAGIC, 8);
    store_dir()->nslots = STORE_NSLOTS;
}

static store_slot_t *store_find(const char *name)
{
    store_dir_t *d = store_dir();
    for (int i = 0; i < STORE_NSLOTS; i++) {
        if (d->slots[i].name[0] && !strcmp(d->slots[i].name, name))
            return &d->slots[i];
    }
    return NULL;
}

/* Push the whole shadow to SRAM, then read it back to confirm it persisted
 * (a cart with the wrong save type, or none, swallows the write silently).
 * Returns 1 if SRAM now matches the shadow, 0 otherwise. */
static int store_flush(void)
{
    if (g_save_state == SAVE_ABSENT)
        return 0;

    sram_write(0, g_shadow, SRAM_SIZE);

    sram_read(0, g_verify, SRAM_SIZE);
    if (memcmp(g_verify, g_shadow, SRAM_SIZE) != 0) {
        debugf("[pd_save] SRAM write did not persist\n");
        if (g_save_state != SAVE_PRESENT)   /* never proven to work -> give up */
            g_save_state = SAVE_ABSENT;
        return 0;
    }

    g_save_state = SAVE_PRESENT;
    return 1;
}

/* Rebuild the data region so every live slot is packed in slot order, with
 * `name` set to (data,len) -- or dropped entirely when data == NULL. */
static int store_rebuild(const char *name, const void *data, int len)
{
    store_dir_t *d = store_dir();

    /* Size the packed result up front so the scratch buffer is only as big
     * as the saves actually are (a few KiB), not the whole data region. */
    uint32_t total = (data != NULL) ? (uint32_t)len : 0;
    int replaced = (data == NULL);
    for (int i = 0; i < STORE_NSLOTS; i++) {
        store_slot_t *sl = &d->slots[i];
        if (!sl->name[0])
            continue;
        if (!strcmp(sl->name, name)) { replaced = 1; continue; }
        total += sl->len;
    }
    if (!replaced && data == NULL)
        return 1;                         /* delete of a missing slot: nop */
    if (total > STORE_DATA_CAP)
        return 0;

    uint8_t *scratch = malloc(total ? total : 1);
    if (!scratch)
        return 0;

    store_slot_t out[STORE_NSLOTS];
    memset(out, 0, sizeof(out));
    uint32_t cursor = 0;
    int n = 0;
    int wrote_target = (data == NULL);   /* nothing to place for a delete */

    for (int i = 0; i < STORE_NSLOTS; i++) {
        store_slot_t *sl = &d->slots[i];
        if (!sl->name[0])
            continue;

        const void *src;
        uint32_t    slen;
        if (!strcmp(sl->name, name)) {
            if (data == NULL)
                continue;                /* delete: skip the old copy */
            src = data;  slen = (uint32_t)len;  wrote_target = 1;
        } else {
            src = store_data() + sl->off;  slen = sl->len;
        }

        if (cursor + slen > STORE_DATA_CAP) { free(scratch); return 0; }
        memcpy(scratch + cursor, src, slen);
        strncpy(out[n].name, sl->name, sizeof(out[n].name) - 1);
        out[n].off = cursor;
        out[n].len = slen;
        cursor += slen;
        n++;
    }

    if (!wrote_target) {                  /* brand-new slot */
        if (n >= STORE_NSLOTS)            { free(scratch); return 0; }
        if (cursor + (uint32_t)len > STORE_DATA_CAP) { free(scratch); return 0; }
        memcpy(scratch + cursor, data, len);
        strncpy(out[n].name, name, sizeof(out[n].name) - 1);
        out[n].off = cursor;
        out[n].len = (uint32_t)len;
        cursor += (uint32_t)len;
        n++;
    }

    memset(&d->slots, 0, sizeof(d->slots));
    memcpy(d->slots, out, sizeof(out));
    memcpy(store_data(), scratch, cursor);
    free(scratch);
    return 1;
}

/* --------------------------------------------------------------- public API */

void PD_SaveInit(void)
{
    sram_bus_timing();

    sram_read(0, g_shadow, SRAM_SIZE);

    if (memcmp(store_dir()->magic, STORE_MAGIC, 8) == 0) {
        g_save_state = SAVE_PRESENT;      /* our header is already on the cart */
        return;
    }

    debugf("[pd_save] formatting save store\n");
    memset(g_shadow, 0, SRAM_SIZE);
    store_format();
    if (!store_flush())
        debugf("[pd_save] SRAM not writable -- saves are session-only\n");
}

const char *PD_SaveCanon(const char *path)
{
    static const char *const names[] = { "Config", "Player", "Player2", "World" };
    if (!path)
        return NULL;
    if (path[0] == '/')
        path++;
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        if (!strcmp(path, names[i]))
            return names[i];
    }
    return NULL;
}

int PD_SaveRead(const char *name, void **out, int *len)
{
    store_slot_t *sl = store_find(name);
    if (!sl)
        return 0;

    void *buf = malloc(sl->len ? sl->len : 1);
    if (!buf)
        return 0;
    memcpy(buf, store_data() + sl->off, sl->len);
    *out = buf;
    *len = (int)sl->len;
    return 1;
}

int PD_SaveWrite(const char *name, const void *data, int len)
{
    if (len < 0 || (uint32_t)len > STORE_DATA_CAP)
        return 0;
    if (!store_rebuild(name, data, len))
        return 0;
    store_flush();
    return 1;
}

int PD_SaveRemove(const char *name)
{
    const char *canon = PD_SaveCanon(name);
    if (!canon)
        return -1;
    if (!store_find(canon))
        return 0;                        /* already gone */
    store_rebuild(canon, NULL, 0);
    store_flush();
    return 0;
}
