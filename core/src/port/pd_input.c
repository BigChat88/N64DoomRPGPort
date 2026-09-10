/*
 * pd_input.c  --  Nintendo 64 controller -> DoomRPG AVK key events.
 *
 * DoomRPG-RE is edge-driven: Main.c watches for a change in the "current key"
 * and calls DoomCanvas_keyPressed() with an AVK_* code (optionally OR'd with
 * AVK_MENU_* flag bits that drive the in-game menus).  We reproduce that by
 * translating N64 button *press* edges into the same codes and queueing them
 * for src/port/main.c to drain.
 *
 * The mobile original was built around a numeric keypad; this is a first-pass
 * mapping good enough to get through the intro and menus.  A proper rebinding
 * screen comes later.
 */
#include <libdragon.h>
#include <SDL.h>

#undef true
#undef false
#undef bool

#include "DoomRPG.h"
#include "DoomCanvas.h"
#include "SDL_Video.h"

extern DoomRPG_t *doomRpg;

/* B acts as "back" inside menus/dialogs but must NOT pop the menu open during
 * play -- only Start does that (the engine reaches menu-back only through the
 * same MENU_OPEN action that also opens it, so we gate on the canvas state). */
static int b_is_back_context(void)
{
    if (!doomRpg || !doomRpg->doomCanvas) return 0;
    switch (doomRpg->doomCanvas->state) {
        case ST_MENU:
        case ST_DIALOG:
            return 1;
        default:
            return 0;
    }
}

/* During play B toggles the automap (Start is the menu opener).  ST_AUTOMAP is
 * included so the same press closes it again. */
static int in_play_context(void)
{
    if (!doomRpg || !doomRpg->doomCanvas) return 0;
    switch (doomRpg->doomCanvas->state) {
        case ST_PLAYING:
        case ST_AUTOMAP:
            return 1;
        default:
            return 0;
    }
}

/* The code-entry prompt has no keyboard on N64: up/down spin the current
 * digit, A commits it, and B deletes the previous one.  A plain AVK_CLR maps
 * to the engine's "erase last digit" action there (and must NOT carry
 * AVK_MENU_OPEN, which would abort the prompt). */
static int in_password_entry(void)
{
    return doomRpg && doomRpg->doomCanvas &&
           doomRpg->doomCanvas->state == ST_DIALOGPASSWORD;
}

static int in_menu_context(void)
{
    return doomRpg && doomRpg->doomCanvas &&
           doomRpg->doomCanvas->state == ST_MENU;
}

#define QUEUE_LEN 16
static int  s_queue[QUEUE_LEN];
static int  s_head, s_tail;
static int  s_stick_latch_x, s_stick_latch_y;

static void push(int code)
{
    int n = (s_tail + 1) % QUEUE_LEN;
    if (n == s_head) return;          /* full -- drop */
    s_queue[s_tail] = code;
    s_tail = n;
}

int PD_PollKey(void)
{
    if (s_head == s_tail) return 0;
    int v = s_queue[s_head];
    s_head = (s_head + 1) % QUEUE_LEN;
    return v;
}

/* --- Rumble Pak ---------------------------------------------------------
 * The engine (DoomCanvas_vibrate) asks for a buzz of N ms.  The N64 Rumble
 * Pak is a plain on/off motor driven by joybus accessory writes; a single
 * write can be dropped and libdragon needs a few polls to finish detecting
 * the pak, so rumble_tick() (called from PD_InputPoll every frame) re-asserts
 * the motor a few times a second while the buzz runs and clears it when the
 * deadline -- in real milliseconds, from the same clock main.c uses -- passes.
 * joypad_set_rumble_active() is a safe no-op when no rumble pak is present. */
static int          s_rumble_on;
static unsigned int s_rumble_until_ms;
static unsigned int s_rumble_last_ms;

void PD_Rumble(int durationMs)
{
    if (durationMs <= 0) return;
    if (durationMs > 1000) durationMs = 1000;
    unsigned int deadline = DoomRPG_GetUpTimeMS() + (unsigned int)durationMs;
    if (!s_rumble_on || (int)(deadline - s_rumble_until_ms) > 0) {
        s_rumble_until_ms = deadline;
    }
    s_rumble_on = 1;
    s_rumble_last_ms = 0;   /* force an immediate assert in rumble_tick() */
}

/* Diagnostic for the Controller menu: has libdragon detected a Rumble Pak on
 * port 1?  If this is false, PD_Rumble() is a silent no-op no matter what the
 * game asks for -- the pak is missing, not seated, or (on emulator) the port
 * is not configured with a Rumble Pak accessory. */
int PD_RumbleSupported(void)
{
    return joypad_get_rumble_supported(JOYPAD_PORT_1) ? 1 : 0;
}

static void rumble_tick(void)
{
    unsigned int now;

    if (!s_rumble_on) return;
    now = DoomRPG_GetUpTimeMS();

    if ((int)(now - s_rumble_until_ms) >= 0) {
        joypad_set_rumble_active(JOYPAD_PORT_1, 0);
        s_rumble_on = 0;
        return;
    }
    if (s_rumble_last_ms == 0 || (now - s_rumble_last_ms) >= 80) {
        joypad_set_rumble_active(JOYPAD_PORT_1, 1);
        s_rumble_last_ms = now;
    }
}

void PD_InputPoll(void)
{
    joypad_poll();
    rumble_tick();
    joypad_buttons_t p = joypad_get_buttons_pressed(JOYPAD_PORT_1);
    joypad_buttons_t h = joypad_get_buttons_held(JOYPAD_PORT_1);
    joypad_inputs_t  in = joypad_get_inputs(JOYPAD_PORT_1);

    /* C-Down passes the turn in play (the engine's PASSTURN action).  It is
     * consumed here so the navigation block below does not also read it as a
     * step back -- D-Down still does that.  In menus C-Down is left alone and
     * keeps acting as "menu down". */
    if (p.c_down && in_play_context()) {
        push(AVK_PASSTURN);
        p.c_down = 0;
    }

    /* Debug menu: the engine opens MENU_DEBUG when the digits 3-6-6-6 are
     * entered in a menu (MenuSystem_enterDigit -> cheatCombo 6663), but the
     * N64 pad has no number keys.  Hold C-Up + C-Right + D-Left + L together
     * with a menu open to inject that sequence.  (The old L+R+C-Up+D-Up combo
     * collided with the PixelFX OSD hotkey.)  From MENU_DEBUG: Change Map ->
     * Items is the test level (items.bsp); it also has a Cheats submenu. */
    static int s_dbg_latch;
    if (h.c_up && h.c_right && h.d_left && h.l && in_menu_context()) {
        if (!s_dbg_latch) {
            push(AVK_0 + 3);
            push(AVK_0 + 6);
            push(AVK_0 + 6);
            push(AVK_0 + 6);
            s_dbg_latch = 1;
        }
        p.c_up    = 0;             /* consume: don't also drive the menu */
        p.c_right = 0;
        p.d_left  = 0;
        p.l       = 0;
    } else if (!(h.c_up && h.c_right && h.d_left && h.l)) {
        s_dbg_latch = 0;
    }

    /* --- navigation / menus --- */
    if (p.d_up   || p.c_up)   push(AVK_UP    | AVK_MENU_UP);
    if (p.d_down || p.c_down) push(AVK_DOWN  | AVK_MENU_DOWN);
    if (p.d_left)             push(AVK_LEFT  | AVK_MENU_PAGE_UP);
    if (p.d_right)            push(AVK_RIGHT | AVK_MENU_PAGE_DOWN);
    /* C-left/right strafe (lateral move, no turn) in play; they have no menu
     * role -- paging is on the d-pad's AVK_MENU_PAGE_* bits. */
    if (p.c_left)             push(AVK_MOVELEFT);
    if (p.c_right)            push(AVK_MOVERIGHT);

    /* --- actions --- */
    if (p.a)     push(AVK_SELECT | AVK_MENU_SELECT);   /* confirm / attack / advance */
    if (p.b) {
        if (in_password_entry())      push(AVK_CLR);                   /* erase last digit */
        else if (in_play_context())   push(AVK_AUTOMAP);               /* toggle the map */
        else if (b_is_back_context()) push(AVK_CLR | AVK_MENU_OPEN);   /* back */
        else                          push(AVK_CLR);                   /* cancel, never opens menu */
    }
    if (p.z)     push(AVK_SELECT);                      /* fire */
    if (p.start) push(AVK_MENUOPEN | AVK_MENU_OPEN);    /* the only menu opener */
    if (p.l)     push(AVK_PREVWEAPON);
    if (p.r)     push(AVK_NEXTWEAPON);

    /* --- analog stick as a coarse d-pad (edge-latched) --- */
    const int TH = 48;
    int sx = (in.stick_x >  TH) ? 1 : (in.stick_x < -TH) ? -1 : 0;
    int sy = (in.stick_y >  TH) ? 1 : (in.stick_y < -TH) ? -1 : 0;
    if (sy != s_stick_latch_y) {
        if (sy > 0) push(AVK_UP   | AVK_MENU_UP);
        if (sy < 0) push(AVK_DOWN | AVK_MENU_DOWN);
        s_stick_latch_y = sy;
    }
    if (sx != s_stick_latch_x) {
        if (sx > 0) push(AVK_RIGHT);
        if (sx < 0) push(AVK_LEFT);
        s_stick_latch_x = sx;
    }
}

int PD_WantsQuit(void)
{
    joypad_buttons_t h = joypad_get_buttons_held(JOYPAD_PORT_1);
    return (h.l && h.r && h.start && h.z);   /* deliberate escape hatch */
}

/* DoomRPG_getEventKey()/DoomRPG_setBind() index an SDL keyboard-state array.
 * There is no keyboard on N64: hand back an all-zero table so their scan
 * loops simply match nothing. */
const Uint8 *SDL_GetKeyboardState(int *numkeys)
{
    static const Uint8 zero[SDL_NUM_SCANCODES];
    if (numkeys) *numkeys = SDL_NUM_SCANCODES;
    return zero;
}
