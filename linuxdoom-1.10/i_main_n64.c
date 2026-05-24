// N64 entrypoint for the DOOM port.

#include <libdragon.h>

#include "doomdef.h"
#include "m_argv.h"
#include "d_main.h"

static char* n64_argv[] = { "doom", NULL };

int main(void)
{
    debug_init_isviewer();
    debug_init_usblog();
    timer_init();

    assertf(is_memory_expanded(), "Expansion Pak (8MB) required");

    dfs_init(DFS_DEFAULT_LOCATION);
    joypad_init();

    myargc = 1;
    myargv = n64_argv;

    D_DoomMain();
    return 0;
}
