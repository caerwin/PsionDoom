/* Emacs style mode select   -*- C++ -*-
 *-----------------------------------------------------------------------------
 *
 *
 *  PrBoom: a Doom port merged with LxDoom and LSDLDoom
 *  based on BOOM, a modified and improved DOOM engine
 *  Copyright (C) 1999 by
 *  id Software, Chi Hoang, Lee Killough, Jim Flynn, Rand Phares, Ty Halderman
 *  Copyright (C) 1999-2006 by
 *  Jess Haas, Nicolas Kalkhof, Colin Phipps, Florian Schulze
 *  Copyright 2005, 2006 by
 *  Florian Schulze, Colin Phipps, Neil Stevens, Andrey Budko
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 *  02111-1307, USA.
 *
 * DESCRIPTION:
 *  DOOM graphics stuff for SDL
 *
 *-----------------------------------------------------------------------------
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include <math.h>

#include "m_argv.h"
#include "doomstat.h"
#include "doomdef.h"
#include "doomtype.h"
#include "v_video.h"
#include "r_draw.h"
#include "d_main.h"
#include "d_event.h"
#include "i_joy.h"
#include "i_video.h"
#include "i_sound.h"
#include "z_zone.h"
#include "s_sound.h"
#include "sounds.h"
#include "w_wad.h"
#include "st_stuff.h"
#include "lprintf.h"

#include "i_system_e32.h"

extern void M_QuitDOOM(int choice);

int use_fullscreen;
int desired_fullscreen;


////////////////////////////////////////////////////////////////////////////
// Input code
int             leds_always_off = 0; // Expected by m_misc, not relevant

// Mouse handling
extern int     usemouse;        // config file var
static boolean mouse_enabled; // usemouse, but can be overriden by -nomouse
static boolean mouse_currently_grabbed;

unsigned char* current_pallete = NULL;
unsigned char* pallete_list[32];

//
// I_StartTic
//

void I_StartTic (void)
{
	I_ProcessKeyEvents();
}

//
// I_StartFrame
//
void I_StartFrame (void)
{
	I_UpdateMusic();
}


boolean I_StartDisplay(void)
{
  return true;
}

void I_EndDisplay(void)
{

}


//
// I_InitInputs
//

static void I_InitInputs(void)
{
  int nomouse_parm = M_CheckParm("-nomouse");

  // check if the user wants to use the mouse
  mouse_enabled = usemouse && !nomouse_parm;

  I_InitJoystick();
}
/////////////////////////////////////////////////////////////////////////////


///////////////////////////////////////////////////////////
// Palette stuff.
//
static void I_UploadNewPalette(int pal)
{
  // This is used to replace the current 256 colour cmap with a new one
  // Used by 256 colour PseudoColor modes
  
	int pplump;


	const byte* palette;

	unsigned int num_pals;

	unsigned int i;

	if(pal > 31)
	{
		pal = 0;
	}
		
	if(pallete_list[pal] == NULL)
	{
		current_pallete = pallete_list[pal] = (unsigned char*)malloc(256);
	}
	else
	{
		current_pallete = pallete_list[pal];
		return;
	}
		  
	pplump = W_GetNumForName("PLAYPAL");
	
	palette = W_CacheLumpNum(pplump);
		
    num_pals = W_LumpLength(pplump) / (3*256);
	
	palette += (pal*256*3);

	// set the colormap entries
	for (i=0; i<256; i++)
	{
#if 0
		//Fast version.
		unsigned int r = palette[0];
		unsigned int g = palette[1];
		unsigned int b = palette[2];

		byte y = (byte)(((r+r+r+b+g+g+g+g)>>3) >> 4);
#else
		//Y = 0.2126 * R + 0.7152 * G + 0.0722 * B;
		//Nice version.

		double r = (double)(palette[0]) / 255.0;
		double g = (double)(palette[1]) / 255.0;
		double b = (double)(palette[2]) / 255.0;

		double yd = (((r * 0.3) + (g * 0.59) + (b * 0.11)) * 15.0);

		byte y = (byte) (yd+0.5);

#endif

		//Since we are in 4bit grey, we fill the upper (unused) 4bits with a copy
		//of the lower. This saves us a lookup and shift in the blit.
		current_pallete[i] = (y | (y << 4));

		palette += 3;
	}
	
	W_UnlockLumpNum(pplump);
	
#ifdef RANGECHECK
	if ((size_t)pal >= num_pals)
		I_Error("I_UploadNewPalette: Palette number out of range (%d>=%d)", pal, num_pals);
#endif
}

//////////////////////////////////////////////////////////////////////////////
// Graphics API

void I_ShutdownGraphics(void)
{
}

//
// I_UpdateNoBlit
//
void I_UpdateNoBlit (void)
{
}

//
// I_FinishUpdate
//
static int newpal = 0;
#define NO_PALETTE_CHANGE 1000

void I_FinishUpdate (void)
{
	if (newpal != NO_PALETTE_CHANGE) 
	{
		I_UploadNewPalette(newpal);
		newpal = NO_PALETTE_CHANGE;
	}

	if(noblit)
		return;

	I_FinishUpdate_e32(screens[0].data, current_pallete, SCREENWIDTH, SCREENHEIGHT);
}

//
// I_ScreenShot - moved to i_sshot.c
//

//
// I_SetPalette
//
void I_SetPalette (int pal)
{
	newpal = pal;
}


void I_PreInitGraphics(void)
{
	I_InitScreen_e32();
}

// CPhipps -
// I_SetRes
// Sets the screen resolution
void I_SetRes(void)
{
  int i;

  // set first three to standard values
  for (i=0; i<3; i++) 
  {
    screens[i].width = SCREENWIDTH;
    screens[i].height = SCREENHEIGHT;
    screens[i].byte_pitch = SCREENPITCH;
  }

  // statusbar
  screens[4].width = SCREENWIDTH;
  screens[4].height = (ST_SCALED_HEIGHT+1);
  screens[4].byte_pitch = SCREENPITCH;

  lprintf(LO_INFO,"I_SetRes: Using resolution %dx%d\n", SCREENWIDTH, SCREENHEIGHT);
}

void I_InitGraphics(void)
{
  char titlebuffer[2048];
  static int    firsttime=1;

  if (firsttime)
  {
    firsttime = 0;

    atexit(I_ShutdownGraphics);
    lprintf(LO_INFO, "I_InitGraphics: %dx%d\n", SCREENWIDTH, SCREENHEIGHT);

    /* Set the video mode */
    I_UpdateVideoMode();

    /* Setup the window title */
    strcpy(titlebuffer,PACKAGE);
    strcat(titlebuffer," ");
    strcat(titlebuffer,VERSION);

    /* Initialize the input system */
    I_InitInputs();

	I_CreateBackBuffer_e32();
  }


}

int I_GetModeFromString(const char *modestr)
{
  return VID_MODE8;
}

void I_UpdateVideoMode(void)
{
  int i;
  video_mode_t mode;

  lprintf(LO_INFO, "I_UpdateVideoMode: %dx%d (%s)\n", SCREENWIDTH, SCREENHEIGHT, desired_fullscreen ? "fullscreen" : "nofullscreen");

  mode = I_GetModeFromString(default_videomode);
  if ((i=M_CheckParm("-vidmode")) && i<myargc-1) {
    mode = I_GetModeFromString(myargv[i+1]);
  }

  V_InitMode(mode);
  V_FreeScreens();

  I_SetRes();

  lprintf(LO_INFO, "I_UpdateVideoMode:\n");

  mouse_currently_grabbed = false;


  V_AllocScreens();

  R_InitBuffer(SCREENWIDTH, SCREENHEIGHT);
}

// CPhipps -
// I_CalculateRes
// Calculates the screen resolution, possibly using the supplied guide
void I_CalculateRes(unsigned int width, unsigned int height)
{
	SCREENWIDTH = I_GetVideoWidth_e32();
	SCREENHEIGHT = I_GetVideoHeight_e32();
	SCREENPITCH = SCREENWIDTH;
}


//
// I_ScreenShot - BMP version
//

int I_ScreenShot (const char *fname)
{
	return 0;
}