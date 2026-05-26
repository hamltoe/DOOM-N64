// N64 video and input layer using libdragon + RDPQ.

#include <stdlib.h>
#include <string.h>

#include <libdragon.h>

#include "doomdef.h"
#include "doomstat.h"
#include "d_main.h"
#include "i_system.h"
#include "i_video.h"
#include "v_video.h"
#include "n64_debug.h"

static boolean video_initialized;
static surface_t doom_screen8;
static byte* n64_aux_screens[3];
static boolean n64_aux_screen_owned[3];
static uint16_t doom_tlut[256] __attribute__((aligned(8)));
static uint64_t last_menu_present_ms;

#define STICK_ANALOG_MAX 80
#define STICK_ANALOG_DEADZONE 6
#define STICK_MENU_THRESHOLD 16

enum
{
    KEYIDX_MENU,
    KEYIDX_MAP_TOGGLE,
    KEYIDX_PAUSE,
    KEYIDX_A_MENU_ENTER,
    KEYIDX_A_USE,
    KEYIDX_B_MENU_BACK,
    KEYIDX_MENU_CONFIRM_Y,
    KEYIDX_MENU_CONFIRM_N,
    KEYIDX_FIRE,
    KEYIDX_STRAFE_LEFT,
    KEYIDX_STRAFE_RIGHT,
    KEYIDX_SPEED,
    KEYIDX_COUNT
};

static boolean key_state[KEYIDX_COUNT];
static boolean weapon_cycle_down;
static boolean weapon_prev_down;
static boolean weapon_next_down;
static int weapon_cycle_key;
static int weapon_prev_key;
static int weapon_next_key;
static int next_weapon_cycle_key = '1';

static boolean I_IsWeaponKeySelectable(player_t* player, int key)
{
    int newweapon;

    if (key < '1' || key > '7')
        return false;

    newweapon = key - '1';

    if (newweapon == wp_fist
        && player->weaponowned[wp_chainsaw]
        && !(player->readyweapon == wp_chainsaw && player->powers[pw_strength]))
    {
        newweapon = wp_chainsaw;
    }

    if (gamemode == commercial
        && newweapon == wp_shotgun
        && player->weaponowned[wp_supershotgun]
        && player->readyweapon != wp_supershotgun)
    {
        newweapon = wp_supershotgun;
    }

    if (!player->weaponowned[newweapon] || newweapon == player->readyweapon)
        return false;

    if ((newweapon == wp_plasma || newweapon == wp_bfg) && gamemode == shareware)
        return false;

    return true;
}

static int I_GetNextSelectableWeaponKey(void)
{
    int i;
    int key;
    player_t* player;

    if (consoleplayer < 0 || consoleplayer >= MAXPLAYERS || !playeringame[consoleplayer])
        return 0;

    player = &players[consoleplayer];
    key = next_weapon_cycle_key;

    for (i = 0; i < 7; i++)
    {
        if (I_IsWeaponKeySelectable(player, key))
            return key;

        key++;
        if (key > '7')
            key = '1';
    }

    return 0;
}

static int I_WeaponToKey(weapontype_t weapon)
{
    switch (weapon)
    {
        case wp_fist:
        case wp_chainsaw:
            return '1';
        case wp_pistol:
            return '2';
        case wp_shotgun:
        case wp_supershotgun:
            return '3';
        case wp_chaingun:
            return '4';
        case wp_missile:
            return '5';
        case wp_plasma:
            return '6';
        case wp_bfg:
            return '7';
        default:
            return '2';
    }
}

static int I_GetDirectionalWeaponKey(boolean next)
{
    int key;
    int current_key;
    player_t* player;
    weapontype_t current_weapon;

    if (consoleplayer < 0 || consoleplayer >= MAXPLAYERS || !playeringame[consoleplayer])
        return 0;

    player = &players[consoleplayer];
    current_weapon = player->readyweapon;
    if (player->pendingweapon != wp_nochange)
        current_weapon = player->pendingweapon;

    current_key = I_WeaponToKey(current_weapon);

    if (next)
    {
        for (key = current_key + 1; key <= '7'; key++)
            if (I_IsWeaponKeySelectable(player, key))
                return key;
    }
    else
    {
        for (key = current_key - 1; key >= '1'; key--)
            if (I_IsWeaponKeySelectable(player, key))
                return key;
    }

    return 0;
}

static void I_PostKeyEvent(evtype_t type, int key)
{
    event_t event;

    event.type = type;
    event.data1 = key;
    event.data2 = 0;
    event.data3 = 0;

    D_PostEvent(&event);
}

static void I_PostJoystickEvent(int buttons, int x, int y)
{
    event_t event;

    event.type = ev_joystick;
    event.data1 = buttons;
    event.data2 = x;
    event.data3 = y;

    D_PostEvent(&event);
}

static int I_NormalizeStickAxis(int value)
{
    int sign;
    int magnitude;
    int range;

    if (!value)
        return 0;

    sign = (value < 0) ? -1 : 1;
    magnitude = (value < 0) ? -value : value;

    if (magnitude <= STICK_ANALOG_DEADZONE)
        return 0;

    if (magnitude > STICK_ANALOG_MAX)
        magnitude = STICK_ANALOG_MAX;

    range = STICK_ANALOG_MAX - STICK_ANALOG_DEADZONE;
    magnitude = ((magnitude - STICK_ANALOG_DEADZONE) * STICK_ANALOG_MAX + (range / 2)) / range;

    if (magnitude > STICK_ANALOG_MAX)
        magnitude = STICK_ANALOG_MAX;

    return sign * magnitude;
}

static int I_MenuAxisStep(int value)
{
    if (value > STICK_MENU_THRESHOLD)
        return 1;
    if (value < -STICK_MENU_THRESHOLD)
        return -1;
    return 0;
}

static void I_UpdateKeyState(boolean down, int state_index, int keycode)
{
    if (down && !key_state[state_index])
        I_PostKeyEvent(ev_keydown, keycode);
    else if (!down && key_state[state_index])
        I_PostKeyEvent(ev_keyup, keycode);

    key_state[state_index] = down;
}

static void I_UpdateWeaponCycle(boolean down)
{
    int next_key;

    if (down && !weapon_cycle_down)
    {
        next_key = I_GetNextSelectableWeaponKey();
        weapon_cycle_key = next_key;

        if (next_key)
        {
            I_PostKeyEvent(ev_keydown, weapon_cycle_key);

            next_weapon_cycle_key = weapon_cycle_key + 1;
            if (next_weapon_cycle_key > '7')
                next_weapon_cycle_key = '1';
        }
    }
    else if (!down && weapon_cycle_down && weapon_cycle_key)
    {
        I_PostKeyEvent(ev_keyup, weapon_cycle_key);
    }

    weapon_cycle_down = down;
}

static void I_UpdateWeaponSelect(boolean down, boolean next, boolean* select_down, int* select_key)
{
    int key;

    if (down && !*select_down)
    {
        key = I_GetDirectionalWeaponKey(next);
        *select_key = key;
        if (key)
            I_PostKeyEvent(ev_keydown, key);
    }
    else if (!down && *select_down && *select_key)
    {
        I_PostKeyEvent(ev_keyup, *select_key);
    }

    *select_down = down;
}

void I_ShutdownGraphics(void)
{
    int i;

    if (!video_initialized)
        return;

    I_N64LogMemoryStats("i_video:before_shutdown");

    for (i = 0; i < 3; i++)
    {
        if (n64_aux_screen_owned[i] && n64_aux_screens[i])
        {
            free(n64_aux_screens[i]);
        }

        n64_aux_screens[i] = NULL;
        n64_aux_screen_owned[i] = false;
        screens[i + 1] = NULL;
    }

    surface_free(&doom_screen8);
    screens[0] = NULL;
    rdpq_close();

    video_initialized = false;
    I_N64LogMemoryStats("i_video:after_shutdown");
}

void I_StartFrame(void)
{
}

void I_StartTic(void)
{
    joypad_inputs_t inputs;
    joypad_buttons_t buttons;
    joypad_buttons_t pressed;
    int stick_x;
    int stick_y;
    int joy_x;
    int joy_y;

    joypad_poll();
    inputs = joypad_get_inputs(JOYPAD_PORT_1);
    buttons = inputs.btn;
    pressed = joypad_get_buttons_pressed(JOYPAD_PORT_1);

    stick_x = I_NormalizeStickAxis(inputs.stick_x);
    stick_y = I_NormalizeStickAxis(inputs.stick_y);

    if (menuactive)
    {
        joy_x = I_MenuAxisStep(stick_x);
        joy_y = I_MenuAxisStep(-stick_y);
    }
    else
    {
        joy_x = stick_x;
        joy_y = -stick_y;
    }

    I_PostJoystickEvent(0, joy_x, joy_y);

    I_UpdateKeyState(buttons.d_up, KEYIDX_PAUSE, KEY_PAUSE);
    I_UpdateWeaponCycle(buttons.d_down);
    I_UpdateWeaponSelect(buttons.d_left, false, &weapon_prev_down, &weapon_prev_key);
    I_UpdateWeaponSelect(buttons.d_right, true, &weapon_next_down, &weapon_next_key);

    I_UpdateKeyState(buttons.start, KEYIDX_MENU, KEY_ESCAPE);
    I_UpdateKeyState(buttons.l, KEYIDX_MAP_TOGGLE, KEY_TAB);
    // Menu buttons should fire on fresh presses to avoid carry-over from held gameplay inputs.
    I_UpdateKeyState((pressed.a && menuactive), KEYIDX_A_MENU_ENTER, KEY_ENTER);
    I_UpdateKeyState(((buttons.a || buttons.z || buttons.r) && !menuactive), KEYIDX_A_USE, KEY_RCTRL);
    I_UpdateKeyState((pressed.b && menuactive), KEYIDX_B_MENU_BACK, KEY_BACKSPACE);
    I_UpdateKeyState((menuactive && (pressed.c_down || pressed.r || pressed.y)), KEYIDX_MENU_CONFIRM_Y, 'y');
    I_UpdateKeyState((menuactive && (pressed.z || pressed.x)), KEYIDX_MENU_CONFIRM_N, 'n');
    I_UpdateKeyState(((buttons.b || buttons.c_down) && !menuactive), KEYIDX_FIRE, ' ');

    I_UpdateKeyState(buttons.c_left, KEYIDX_STRAFE_LEFT, ',');
    I_UpdateKeyState(buttons.c_right, KEYIDX_STRAFE_RIGHT, '.');
    I_UpdateKeyState(buttons.c_up, KEYIDX_SPEED, KEY_RSHIFT);
}

void I_UpdateNoBlit(void)
{
}

void I_FinishUpdate(void)
{
    surface_t* disp;
    uint64_t now_ms;

    if (!video_initialized)
        return;

    // Menu overlays can trigger back-to-back presents while logic catches up.
    // Pace these to display rate to avoid transient tearing/partial updates.
    if (menuactive && gamestate == GS_LEVEL)
    {
        now_ms = get_ticks_ms();
        if (last_menu_present_ms && (now_ms - last_menu_present_ms) < 16)
            return;
    }

    disp = display_get();
    if (!disp)
        return;

    rdpq_attach_clear(disp, NULL);
    rdpq_set_mode_copy(false);
    rdpq_mode_tlut(TLUT_RGBA16);
    rdpq_tex_upload_tlut(doom_tlut, 0, 256);
    rdpq_tex_blit(&doom_screen8, 0, 20, NULL);
    rdpq_detach_show();

    if (menuactive && gamestate == GS_LEVEL)
        last_menu_present_ms = get_ticks_ms();
}

void I_ReadScreen(byte* scr)
{
    memcpy(scr, screens[0], SCREENWIDTH * SCREENHEIGHT);
}

void I_SetPalette(byte* palette)
{
    int i;

    for (i = 0; i < 256; i++)
    {
        uint8_t r = gammatable[usegamma][palette[0]];
        uint8_t g = gammatable[usegamma][palette[1]];
        uint8_t b = gammatable[usegamma][palette[2]];

        doom_tlut[i] = (uint16_t)(((r >> 3) << 11) |
                                  ((g >> 3) << 6) |
                                  ((b >> 3) << 1) |
                                  1);

        palette += 3;
    }
}

void I_InitGraphics(void)
{
    int i;
    size_t aux_size;

    if (video_initialized)
        return;

    I_N64LogMemoryStats("i_video:before_init");

    uint32_t existing_buffers = display_get_num_buffers();
    N64_DEBUGF("I_InitGraphics: display_get_num_buffers()=%u\n", (unsigned)existing_buffers);
    if (existing_buffers > 0)
    {
        N64_DEBUGF("I_InitGraphics: reusing existing display\n");
    }
    else
    {
        N64_DEBUGF("I_InitGraphics: calling display_init (fresh)\n");
        display_init(RESOLUTION_320x240, DEPTH_16_BPP, 3, GAMMA_NONE, FILTERS_RESAMPLE);
    }

    rdpq_init();

    doom_screen8 = surface_alloc(FMT_CI8, SCREENWIDTH, SCREENHEIGHT);
    if (!doom_screen8.buffer)
        I_Error("I_InitGraphics: failed to allocate %dx%d CI8 surface",
                SCREENWIDTH, SCREENHEIGHT);

    screens[0] = (byte*)doom_screen8.buffer;
    memset(screens[0], 0, SCREENWIDTH * SCREENHEIGHT);

    aux_size = (size_t)SCREENWIDTH * (size_t)SCREENHEIGHT;
    for (i = 0; i < 3; i++)
    {
        n64_aux_screens[i] = NULL;
        n64_aux_screen_owned[i] = false;
        screens[i + 1] = NULL;
    }

    n64_aux_screens[0] = (byte*)malloc(aux_size);
    if (!n64_aux_screens[0])
        I_Error("I_InitGraphics: failed to allocate scratch screen 1");

    n64_aux_screen_owned[0] = true;
    memset(n64_aux_screens[0], 0, aux_size);
    screens[1] = n64_aux_screens[0];

    for (i = 1; i < 3; i++)
    {
        n64_aux_screens[i] = (byte*)malloc(aux_size);
        if (!n64_aux_screens[i])
        {
            // Keep running in low-memory scenarios by aliasing to an existing scratch buffer.
            n64_aux_screens[i] = n64_aux_screens[i - 1];
            n64_aux_screen_owned[i] = false;
            N64_DEBUGF("I_InitGraphics: scratch screen %d fallback alias\n", i + 1);
        }
        else
        {
            n64_aux_screen_owned[i] = true;
            memset(n64_aux_screens[i], 0, aux_size);
        }

        screens[i + 1] = n64_aux_screens[i];
    }

    memset(doom_tlut, 0, sizeof(doom_tlut));
    memset(key_state, 0, sizeof(key_state));
    weapon_cycle_down = false;
    weapon_prev_down = false;
    weapon_next_down = false;
    weapon_cycle_key = 0;
    weapon_prev_key = 0;
    weapon_next_key = 0;
    next_weapon_cycle_key = '1';

    video_initialized = true;
    I_N64LogMemoryStats("i_video:after_init");
}
