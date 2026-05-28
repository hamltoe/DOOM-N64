// Single-player networking backend for N64 bring-up.

#include <stdlib.h>
#include <string.h>

#include "doomdef.h"
#include "doomstat.h"
#include "d_net.h"
#include "i_system.h"
#include "m_argv.h"
#include "i_wad_browser_n64.h"

#include "i_net.h"

void I_InitNetwork(void)
{
    int local_players;
    int p;

    doomcom = (doomcom_t*)malloc(sizeof(*doomcom));
    if (!doomcom)
        I_Error("I_InitNetwork: failed to allocate doomcom");

    memset(doomcom, 0, sizeof(*doomcom));

    p = M_CheckParm("-dup");
    if (p && p < myargc - 1)
    {
        doomcom->ticdup = myargv[p + 1][0] - '0';
        if (doomcom->ticdup < 1)
            doomcom->ticdup = 1;
        if (doomcom->ticdup > 9)
            doomcom->ticdup = 9;
    }
    else
    {
        doomcom->ticdup = 1;
    }

    doomcom->extratics = M_CheckParm("-extratic") ? 1 : 0;

    local_players = I_N64GetSelectedPlayerCount();
    if (local_players < 1)
        local_players = 1;
    if (local_players > MAXPLAYERS)
        local_players = MAXPLAYERS;

    D_SetLocalPlayerCount(local_players);

    doomcom->id = DOOMCOM_ID;
    // Boot as a single player so the title/menus render full-screen normally.
    // G_DoNewGame expands playeringame[] and doomcom->numplayers via
    // D_LocalMultiplayerEnabled() when the user picks a map.
    doomcom->numplayers = 1;
    doomcom->numnodes = 1;
    doomcom->deathmatch = false;
    doomcom->consoleplayer = 0;

    netgame = false;
}

void I_NetCmd(void)
{
    if (doomcom->command == CMD_GET)
    {
        doomcom->remotenode = -1;
        doomcom->datalength = 0;
        return;
    }

    if (doomcom->command == CMD_SEND)
        return;

    I_Error("I_NetCmd: unsupported command %d", doomcom->command);
}
