// Emacs style mode select   -*- C++ -*- 
//-----------------------------------------------------------------------------
//
// $Id:$
//
// Copyright (C) 1993-1996 by id Software, Inc.
//
// This source is available for distribution and/or modification
// only under the terms of the DOOM Source Code License as
// published by id Software. All rights reserved.
//
// The source is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// FITNESS FOR A PARTICULAR PURPOSE. See the DOOM Source Code License
// for more details.
//
// DESCRIPTION:
//	System specific interface stuff.
//
//-----------------------------------------------------------------------------


#ifndef __I_VIDEO__
#define __I_VIDEO__


#include "doomtype.h"

#ifdef __GNUG__
#pragma interface
#endif


// Called by D_DoomMain,
// determines the hardware configuration
// and sets up the video mode
void I_InitGraphics (void);


void I_ShutdownGraphics(void);

// Takes full 8 bit values.
void I_SetPalette (byte* palette);

void I_UpdateNoBlit (void);
void I_FinishUpdate (void);

// Wait for vertical retrace or pause a bit.
void I_WaitVBL(int count);

void I_ReadScreen (byte* scr);

void I_BeginRead (void);
void I_EndRead (void);

#ifdef N64
#define N64_DISPLAY_RESOLUTION ((resolution_t){ \
	.width = 320, \
	.height = 200, \
	.aspect_ratio = 4.0f / 3.0f, \
	.overscan_margin = VI_CRT_MARGIN \
})

typedef struct n64_local_input_s
{
	int joy_x;
	int joy_y;
	boolean strafe_left;
	boolean strafe_right;
	boolean speed;
	boolean fire;
	boolean use;
	int weapon_key;
} n64_local_input_t;

void I_N64GetLocalInputState(int player_index, n64_local_input_t* out_state);
void I_N64SplitScreenBeginFrame(int player_count);
void I_N64SplitScreenEndFrame(void);
int I_N64GetActiveGameplayPort(void);
#endif



#endif
//-----------------------------------------------------------------------------
//
// $Log:$
//
//-----------------------------------------------------------------------------
