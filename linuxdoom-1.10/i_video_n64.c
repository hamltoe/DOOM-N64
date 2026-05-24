// N64 video and input layer using libdragon + RDPQ.

#include <string.h>

#include <libdragon.h>

#include "doomdef.h"
#include "doomstat.h"
#include "d_main.h"
#include "i_system.h"
#include "i_video.h"
#include "v_video.h"

static boolean video_initialized;
static surface_t doom_screen8;
static uint16_t doom_tlut[256] __attribute__((aligned(8)));
static uint64_t last_menu_present_ms;

enum
{
    KEYIDX_UP,
    KEYIDX_DOWN,
    KEYIDX_LEFT,
    KEYIDX_RIGHT,
    KEYIDX_MENU,
    KEYIDX_ENTER,
    KEYIDX_FIRE,
    KEYIDX_USE,
    KEYIDX_STRAFE_LEFT,
    KEYIDX_STRAFE_RIGHT,
    KEYIDX_SPEED,
    KEYIDX_COUNT
};

static boolean key_state[KEYIDX_COUNT];

static void I_PostKeyEvent(evtype_t type, int key)
{
    event_t event;

    event.type = type;
    event.data1 = key;
    event.data2 = 0;
    event.data3 = 0;

    D_PostEvent(&event);
}

static void I_UpdateKeyState(boolean down, int state_index, int keycode)
{
    if (down && !key_state[state_index])
        I_PostKeyEvent(ev_keydown, keycode);
    else if (!down && key_state[state_index])
        I_PostKeyEvent(ev_keyup, keycode);

    key_state[state_index] = down;
}

void I_ShutdownGraphics(void)
{
    if (!video_initialized)
        return;

    surface_free(&doom_screen8);
    rdpq_close();
    display_close();

    video_initialized = false;
}

void I_StartFrame(void)
{
}

void I_StartTic(void)
{
    joypad_inputs_t inputs;
    joypad_buttons_t buttons;
    boolean up;
    boolean down;
    boolean left;
    boolean right;

    joypad_poll();
    inputs = joypad_get_inputs(JOYPAD_PORT_1);
    buttons = inputs.btn;

    up = buttons.d_up || (inputs.stick_y > 45);
    down = buttons.d_down || (inputs.stick_y < -45);
    left = buttons.d_left || (inputs.stick_x < -45);
    right = buttons.d_right || (inputs.stick_x > 45);

    I_UpdateKeyState(up, KEYIDX_UP, KEY_UPARROW);
    I_UpdateKeyState(down, KEYIDX_DOWN, KEY_DOWNARROW);
    I_UpdateKeyState(left, KEYIDX_LEFT, KEY_LEFTARROW);
    I_UpdateKeyState(right, KEYIDX_RIGHT, KEY_RIGHTARROW);

    I_UpdateKeyState(buttons.start, KEYIDX_MENU, KEY_ESCAPE);
    I_UpdateKeyState(buttons.a, KEYIDX_ENTER, KEY_ENTER);
    I_UpdateKeyState(buttons.b, KEYIDX_FIRE, KEY_RCTRL);
    I_UpdateKeyState(buttons.z, KEYIDX_USE, ' ');

    I_UpdateKeyState((buttons.l || buttons.c_left), KEYIDX_STRAFE_LEFT, ',');
    I_UpdateKeyState((buttons.r || buttons.c_right), KEYIDX_STRAFE_RIGHT, '.');
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
    if (video_initialized)
        return;

    display_init(RESOLUTION_320x240, DEPTH_16_BPP, 3, GAMMA_NONE, FILTERS_RESAMPLE);
    rdpq_init();

    doom_screen8 = surface_alloc(FMT_CI8, SCREENWIDTH, SCREENHEIGHT);
    if (!doom_screen8.buffer)
        I_Error("I_InitGraphics: failed to allocate %dx%d CI8 surface",
                SCREENWIDTH, SCREENHEIGHT);

    screens[0] = (byte*)doom_screen8.buffer;
    memset(screens[0], 0, SCREENWIDTH * SCREENHEIGHT);
    memset(doom_tlut, 0, sizeof(doom_tlut));
    memset(key_state, 0, sizeof(key_state));

    video_initialized = true;
}
