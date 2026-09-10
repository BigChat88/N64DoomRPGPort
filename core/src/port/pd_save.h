/*
 * pd_save.h  --  persistent save store for the N64 port.
 *
 * DoomRPG-RE writes four little files through SDL_RWops -- "Config", "Player",
 * "Player2" and "World" -- and expects to read them back on the next boot.
 * The desktop build put them on disk; here they live in the cartridge's
 * 32 KiB SRAM (N64_ROM_SAVETYPE = sram256k), fronted by a RAM shadow.
 *
 * pd_rwops.c turns SDL_RWFromFile() on those names into calls to this API;
 * everything else (the DragonFS asset reads) is untouched.
 */
#ifndef PORT_PD_SAVE_H__
#define PORT_PD_SAVE_H__

/* Bring the SRAM shadow up and load its directory.  Call once at boot,
 * before the game reads its config.  Safe to call with no SRAM present
 * (saves then work for the session only and a warning is logged). */
void PD_SaveInit(void);

/* Canonicalise a path the game passed to SDL_RWFromFile().  Returns a static
 * string ("Config" / "Player" / "Player2" / "World") if it names a save
 * slot, or NULL if it does not (leave those to DragonFS). */
const char *PD_SaveCanon(const char *path);

/* Read slot `name` (already canonical).  On success returns 1 and hands back
 * a malloc'd copy of the contents in *out (caller frees) with its length in
 * *len.  Returns 0 if the slot does not exist. */
int PD_SaveRead(const char *name, void **out, int *len);

/* Replace (or create) slot `name` with `len` bytes from `data`, then flush
 * the changed FlashRAM sectors.  Returns 1 on success, 0 if it does not fit. */
int PD_SaveWrite(const char *name, const void *data, int len);

/* Delete slot `name` and flush.  Accepts a raw path (canonicalised here) so
 * it can back the remove() shim used by Game_deleteSaveFiles().  Returns 0 on
 * success, -1 if the path is not a save slot. */
int PD_SaveRemove(const char *name);

#endif /* PORT_PD_SAVE_H__ */
