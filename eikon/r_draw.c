/* Emacs style mode select   -*- C++ -*-
 *-----------------------------------------------------------------------------
 *
 *
 *  PrBoom: a Doom port merged with LxDoom and LSDLDoom
 *  based on BOOM, a modified and improved DOOM engine
 *  Copyright (C) 1999 by
 *  id Software, Chi Hoang, Lee Killough, Jim Flynn, Rand Phares, Ty Halderman
 *  Copyright (C) 1999-2000 by
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
 *      The actual span/column drawing functions.
 *      Here find the main potential for optimization,
 *       e.g. inline assembly, different algorithms.
 *
 *-----------------------------------------------------------------------------*/

#include "doomstat.h"
#include "w_wad.h"
#include "r_main.h"
#include "r_draw.h"
#include "v_video.h"
#include "st_stuff.h"
#include "g_game.h"
#include "am_map.h"
#include "lprintf.h"

//
// All drawing to the view buffer is accomplished in this file.
// The other refresh files only know about ccordinates,
//  not the architecture of the frame buffer.
// Conveniently, the frame buffer is a linear one,
//  and we need only the base address,
//  and the total size == width*height*depth/8.,
//

int  viewwidth;
int  scaledviewwidth;
int  viewheight;
int  viewwindowx;
int  viewwindowy;

// Color tables for different players,
//  translate a limited part to another
//  (color ramps used for  suit colors).
//

byte *translationtables;


draw_vars_t drawvars = { 
  NULL, // byte_topleft
  0, // byte_pitch
};



//
// Spectre/Invisibility.
//

#define FUZZTABLE 50
// proff 08/17/98: Changed for high-res
//#define FUZZOFF (SCREENWIDTH)
#define FUZZOFF 1

static const int fuzzoffset_org[FUZZTABLE] = {
  FUZZOFF,-FUZZOFF,FUZZOFF,-FUZZOFF,FUZZOFF,FUZZOFF,-FUZZOFF,
  FUZZOFF,FUZZOFF,-FUZZOFF,FUZZOFF,FUZZOFF,FUZZOFF,-FUZZOFF,
  FUZZOFF,FUZZOFF,FUZZOFF,-FUZZOFF,-FUZZOFF,-FUZZOFF,-FUZZOFF,
  FUZZOFF,-FUZZOFF,-FUZZOFF,FUZZOFF,FUZZOFF,FUZZOFF,FUZZOFF,-FUZZOFF,
  FUZZOFF,-FUZZOFF,FUZZOFF,FUZZOFF,-FUZZOFF,-FUZZOFF,FUZZOFF,
  FUZZOFF,-FUZZOFF,-FUZZOFF,-FUZZOFF,-FUZZOFF,FUZZOFF,FUZZOFF,
  FUZZOFF,FUZZOFF,-FUZZOFF,FUZZOFF,FUZZOFF,-FUZZOFF,FUZZOFF
};

static int fuzzoffset[FUZZTABLE];

static int fuzzpos = 0;


void R_SetDefaultDrawColumnVars(draw_column_vars_t *dcvars)
{
	dcvars->x = dcvars->yl = dcvars->yh = dcvars->z = 0;
	dcvars->iscale = dcvars->texturemid = 0;
	dcvars->source = NULL;
	dcvars->colormap = colormaps[0];
	dcvars->translation = NULL;
}

//
// R_InitTranslationTables
// Creates the translation tables to map
//  the green color ramp to gray, brown, red.
// Assumes a given structure of the PLAYPAL.
// Could be read from a lump instead.
//

byte playernumtotrans[MAXPLAYERS];
extern lighttable_t *(*c_zlight)[LIGHTLEVELS][MAXLIGHTZ];

void R_InitTranslationTables (void)
{
	int i, j;
	#define MAXTRANS 3
	byte transtocolour[MAXTRANS];

	// killough 5/2/98:
	// Remove dependency of colormaps aligned on 256-byte boundary

	if (translationtables == NULL) // CPhipps - allow multiple calls
		translationtables = Z_Malloc(256*MAXTRANS, PU_STATIC, 0);

	for (i=0; i<MAXTRANS; i++)
		transtocolour[i] = 255;

	for (i=0; i<MAXPLAYERS; i++)
	{
		byte wantcolour = mapcolor_plyr[i];
		playernumtotrans[i] = 0;
		if (wantcolour != 0x70) // Not green, would like translation
		{
			for (j=0; j<MAXTRANS; j++)
			{
				if (transtocolour[j] == 255)
				{
					transtocolour[j] = wantcolour; 
					playernumtotrans[i] = j+1;
					break;
				}
			}
		}
	}

	// translate just the 16 green colors
	for (i=0; i<256; i++)
	{
		if (i >= 0x70 && i<= 0x7f)
		{
			// CPhipps - configurable player colours
			translationtables[i] = colormaps[0][((i&0xf)<<9) + transtocolour[0]];
			translationtables[i+256] = colormaps[0][((i&0xf)<<9) + transtocolour[1]];
			translationtables[i+512] = colormaps[0][((i&0xf)<<9) + transtocolour[2]];
		}
		else  // Keep all other colors as is.
		{
			translationtables[i]=translationtables[i+256]=translationtables[i+512]=i;
		}
	}
}

//
// A column is a vertical slice/span from a wall texture that,
//  given the DOOM style restrictions on the view orientation,
//  will always have constant z depth.
// Thus a special case loop for very fast rendering can
//  be used. It has also been used with Wolfenstein 3D.
// 
void R_DrawColumn (draw_column_vars_t *dcvars) 
{ 
    unsigned int count = dcvars->yh - dcvars->yl;

	const byte *source = dcvars->source;
	const byte *colormap = dcvars->colormap;

	byte* dest = drawvars.byte_topleft + (dcvars->yl*drawvars.byte_pitch) + dcvars->x;

    const fixed_t		fracstep = dcvars->iscale;
	fixed_t frac = dcvars->texturemid + (dcvars->yl - centery)*fracstep;
 
    // Zero length, column does not exceed a pixel.
    if (dcvars->yl >= dcvars->yh)
		return;
				 
    // Inner loop that does the actual texture mapping,
    //  e.g. a DDA-lile scaling.
    // This is as fast as it gets.
    do 
    {
		// Re-map color indices from wall texture column
		//  using a lighting/special effects LUT.
		*dest = colormap[source[(frac>>FRACBITS)&127]];
	
		dest += SCREENWIDTH;
		frac += fracstep;
    } while (count--); 
} 

//
// Framebuffer postprocessing.
// Creates a fuzzy image by copying pixels
//  from adjacent ones to left and right.
// Used with an all black colormap, this
//  could create the SHADOW effect,
//  i.e. spectres and invisible players.
//
void R_DrawFuzzColumn (draw_column_vars_t *dcvars)
{ 
    unsigned int count;

    byte*		dest;
    fixed_t		frac;
    fixed_t		fracstep;

	int dc_yl = dcvars->yl;
	int dc_yh = dcvars->yh;

    // Adjust borders. Low... 
    if (!dc_yl) 
		dc_yl = 1;

    // .. and high.
    if (dc_yh == viewheight-1) 
		dc_yh = viewheight - 2; 
	
	 count = dc_yh - dc_yl;

    // Zero length, column does not exceed a pixel.
    if (dc_yl >= dc_yh)
		return;
    
	dest = drawvars.byte_topleft + (dcvars->yl*drawvars.byte_pitch) + dcvars->x;


    // Looks familiar.
    fracstep = dcvars->iscale;
	
    frac = dcvars->texturemid + (dc_yl-centery)*fracstep; 

    // Looks like an attempt at dithering,
    //  using the colormap #6 (of 0-31, a bit
    //  brighter than average).
    do 
    {
		// Lookup framebuffer, and retrieve
		//  a pixel that is either one column
		//  left or right of the current one.
		// Add index from colormap to index.
			*dest = fullcolormap[6*256+dest[fuzzoffset[fuzzpos]]]; 

		// Clamp table lookup index.
		if (++fuzzpos == FUZZTABLE) 
			fuzzpos = 0;
	
		dest += SCREENWIDTH;

		frac += fracstep; 
    } while (count--); 
} 


//
// R_DrawTranslatedColumn
// Used to draw player sprites
//  with the green colorramp mapped to others.
// Could be used with different translation
//  tables, e.g. the lighter colored version
//  of the BaronOfHell, the HellKnight, uses
//  identical sprites, kinda brightened up.
//


void R_DrawTranslatedColumn (draw_column_vars_t *dcvars)
{
    unsigned int count = dcvars->yh - dcvars->yl;

	const byte *source = dcvars->source;
	const byte *colormap = dcvars->colormap;
	const byte *translation = dcvars->translation;

	byte* dest = drawvars.byte_topleft + (dcvars->yl*drawvars.byte_pitch) + dcvars->x;

    const fixed_t		fracstep = dcvars->iscale;
	fixed_t frac = dcvars->texturemid + (dcvars->yl - centery)*fracstep;
 
    // Zero length, column does not exceed a pixel.
    if (dcvars->yl >= dcvars->yh)
		return;

    // Here we do an additional index re-mapping.
    do 
    {
		// Translation tables are used
		//  to map certain colorramps to other ones,
		//  used with PLAY sprites.
		// Thus the "green" ramp of the player 0 sprite
		//  is mapped to gray, red, black/indigo. 
		*dest = colormap[translation[source[frac>>FRACBITS]]];
		dest += SCREENWIDTH;
	
		frac += fracstep; 
    } while (count--); 
} 




//
// R_DrawSpan
// With DOOM style restrictions on view orientation,
//  the floors and ceilings consist of horizontal slices
//  or spans with constant z depth.
// However, rotation around the world z axis is possible,
//  thus this mapping, while simpler and faster than
//  perspective correct texture mapping, has to traverse
//  the texture at an angle in all but a few cases.
// In consequence, flats are not stored by column (like walls),
//  and the inner loop has to step in texture space u and v.
//
void R_DrawSpan(draw_span_vars_t *dsvars)
{
	unsigned int count = (dsvars->x2 - dsvars->x1);
	
	const byte *source = dsvars->source;
	const byte *colormap = dsvars->colormap;
	
	byte* dest = drawvars.byte_topleft + dsvars->y*drawvars.byte_pitch + dsvars->x1;
	
	const unsigned int step = ((dsvars->xstep << 10) & 0xffff0000) | ((dsvars->ystep >> 6)  & 0x0000ffff);

    unsigned int position = ((dsvars->xfrac << 10) & 0xffff0000) | ((dsvars->yfrac >> 6)  & 0x0000ffff);

	unsigned int xtemp, ytemp, spot;

    do
    {
		// Calculate current texture index in u,v.
        ytemp = (position >> 4) & 0x0fc0;
        xtemp = (position >> 26);
        spot = xtemp | ytemp;

		// Lookup pixel from flat texture tile,
		//  re-index using light/colormap.
		*dest++ = colormap[source[spot]];

        position += step;

	} while (count--);
}

//
// R_InitBuffer
// Creats lookup tables that avoid
//  multiplies and other hazzles
//  for getting the framebuffer address
//  of a pixel to draw.
//

void R_InitBuffer(int width, int height)
{
	int i=0;
	// Handle resize,
	//  e.g. smaller view windows
	//  with border and/or status bar.

	viewwindowx = (SCREENWIDTH-width) >> 1;

	// Same with base row offset.

	viewwindowy = width==SCREENWIDTH ? 0 : (SCREENHEIGHT-(ST_SCALED_HEIGHT-1)-height)>>1;

	drawvars.byte_topleft = screens[0].data + viewwindowy*screens[0].byte_pitch + viewwindowx;
	drawvars.byte_pitch = screens[0].byte_pitch;

	for (i=0; i<FUZZTABLE; i++)
		fuzzoffset[i] = fuzzoffset_org[i]*screens[0].byte_pitch;
}

//
// R_FillBackScreen
// Fills the back screen with a pattern
//  for variable screen sizes
// Also draws a beveled edge.
//
// CPhipps - patch drawing updated

void R_FillBackScreen (void)
{
	int     x,y;

	if (scaledviewwidth == SCREENWIDTH)
		return;

	V_DrawBackground(gamemode == commercial ? "GRNROCK" : "FLOOR7_2", 1);

	for (x=0; x<scaledviewwidth; x+=8)
		V_DrawNamePatch(viewwindowx+x,viewwindowy-8,1,"brdr_t", CR_DEFAULT, VPT_NONE);

	for (x=0; x<scaledviewwidth; x+=8)
		V_DrawNamePatch(viewwindowx+x,viewwindowy+viewheight,1,"brdr_b", CR_DEFAULT, VPT_NONE);

	for (y=0; y<viewheight; y+=8)
		V_DrawNamePatch(viewwindowx-8,viewwindowy+y,1,"brdr_l", CR_DEFAULT, VPT_NONE);

	for (y=0; y<viewheight; y+=8)
		V_DrawNamePatch(viewwindowx+scaledviewwidth,viewwindowy+y,1,"brdr_r", CR_DEFAULT, VPT_NONE);

	// Draw beveled edge.
	V_DrawNamePatch(viewwindowx-8,viewwindowy-8,1,"brdr_tl", CR_DEFAULT, VPT_NONE);

	V_DrawNamePatch(viewwindowx+scaledviewwidth,viewwindowy-8,1,"brdr_tr", CR_DEFAULT, VPT_NONE);

	V_DrawNamePatch(viewwindowx-8,viewwindowy+viewheight,1,"brdr_bl", CR_DEFAULT, VPT_NONE);

	V_DrawNamePatch(viewwindowx+scaledviewwidth,viewwindowy+viewheight,1,"brdr_br", CR_DEFAULT, VPT_NONE);
}

//
// Copy a screen buffer.
//

void R_VideoErase(int x, int y, int count)
{
    memcpy(screens[0].data+y*screens[0].byte_pitch+x*V_GetPixelDepth(),
           screens[1].data+y*screens[1].byte_pitch+x*V_GetPixelDepth(),
           count*V_GetPixelDepth());   // LFB copy.
}

//
// R_DrawViewBorder
// Draws the border around the view
//  for different size windows?
//

void R_DrawViewBorder(void)
{
	int top, side, i;

	if ((SCREENHEIGHT != viewheight) || ((automapmode & am_active) && ! (automapmode & am_overlay)))
	{
		// erase left and right of statusbar
		side= ( SCREENWIDTH - ST_SCALED_WIDTH ) / 2;

		if (side > 0)
		{
			for (i = (SCREENHEIGHT - ST_SCALED_HEIGHT); i < SCREENHEIGHT; i++)
			{
				R_VideoErase (0, i, side);
				R_VideoErase (ST_SCALED_WIDTH+side, i, side);
			}
		}
	}

	if ( viewheight >= ( SCREENHEIGHT - ST_SCALED_HEIGHT ))
		return; // if high-res, don�t go any further!

	top = ((SCREENHEIGHT-ST_SCALED_HEIGHT)-viewheight)/2;
	side = (SCREENWIDTH-scaledviewwidth)/2;

	// copy top
	for (i = 0; i < top; i++)
		R_VideoErase (0, i, SCREENWIDTH);

	// copy sides
	for (i = top; i < (top+viewheight); i++)
	{
		R_VideoErase (0, i, side);
		R_VideoErase (viewwidth+side, i, side);
	}

	// copy bottom
	for (i = top+viewheight; i < (SCREENHEIGHT - ST_SCALED_HEIGHT); i++)
		R_VideoErase (0, i, SCREENWIDTH);
}
