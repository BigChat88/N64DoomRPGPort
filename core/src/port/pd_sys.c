/*
 * pd_sys.c  --  misc SDL entry points (init, timing, logging, message box)
 *               backed by libdragon / newlib.
 */
#include <libdragon.h>
#include <SDL.h>

void PD_FatalMessage(const char *title, const char *msg);   /* pd_video.c */

/* ------------------------------------------------------------- subsystems */
int  SDL_Init(Uint32 flags)          { (void)flags; return 0; }
int  SDL_InitSubSystem(Uint32 flags) { (void)flags; return 0; }
void SDL_Quit(void)                  { }
void SDL_QuitSubSystem(Uint32 flags) { (void)flags; }

/* --------------------------------------------------------------- timing */
Uint32 SDL_GetTicks(void) { return (Uint32)get_ticks_ms(); }

void SDL_Delay(Uint32 ms)
{
    uint64_t end = get_ticks_ms() + ms;
    while (get_ticks_ms() < end) { /* spin */ }
}

/* --------------------------------------------------------------- logging */
static char s_error[256];

const char *SDL_GetError(void) { return s_error; }

int SDL_SetError(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s_error, sizeof(s_error), fmt, ap);
    va_end(ap);
    return -1;
}

void SDL_Log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

/* ----------------------------------------------------------- message box */
int SDL_ShowMessageBox(const SDL_MessageBoxData *data, int *buttonid)
{
    const char *title = (data && data->title)   ? data->title   : "Message";
    const char *msg   = (data && data->message) ? data->message : "";
    fprintf(stderr, "\n=== %s ===\n%s\n", title, msg);
    if (buttonid) *buttonid = 0;
    /* DoomRPG only pops a message box on a fatal error, right before it tears
     * the display down and exit(0)s.  Show it on screen and halt here so the
     * cause is visible on hardware instead of a black screen. */
    PD_FatalMessage(title, msg);
    return 0;
}

int SDL_ShowSimpleMessageBox(Uint32 flags, const char *title, const char *message, SDL_Window *window)
{
    (void)flags; (void)window;
    fprintf(stderr, "\n=== %s ===\n%s\n", title ? title : "Message", message ? message : "");
    PD_FatalMessage(title ? title : "Message", message ? message : "");
    return 0;
}
