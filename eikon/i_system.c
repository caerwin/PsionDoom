/* Emacs style mode select   -*- C++ -*-
 *-----------------------------------------------------------------------------
 *
 *
 *  PrBoom: a Doom port merged with LxDoom and LSDLDoom
 *  based on BOOM, a modified and improved DOOM engine
 *  Copyright (C) 1999 by
 *  id Software, Chi Hoang, Lee Killough, Jim Flynn, Rand Phares, Ty Halderman
 *  Copyright (C) 1999-2006 by Colin Phipps, Florian Schulze
 *  
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
 *  Misc system stuff needed by Doom, implemented for POSIX systems.
 *  Timers and signals.
 *
 *-----------------------------------------------------------------------------
 */


#include <unistd.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <errno.h>

#include "doomtype.h"
#include "m_fixed.h"
#include "r_fps.h"
#include "i_system.h"
#include "doomdef.h"
#include "lprintf.h"

#include "i_system.h"
#include "i_system_e32.h"


static unsigned int start_displaytime;
static unsigned int displaytime;

int ms_to_next_tick;


void I_uSleep(unsigned long usecs)
{
	I_uSleep_e32(usecs);
}

/* CPhipps - believe it or not, it is possible with consecutive calls to 
 * gettimeofday to receive times out of order, e.g you query the time twice and 
 * the second time is earlier than the first. Cheap'n'cheerful fix here.
 * NOTE: only occurs with bad kernel drivers loaded, e.g. pc speaker drv
 */

static unsigned long lasttimereply;
static unsigned long basetime;

int I_GetTime_RealTime (void)
{
  struct timeval tv;
  struct timezone tz;
  unsigned long thistimereply;

  gettimeofday(&tv, &tz);

  thistimereply = (tv.tv_sec * TICRATE + (tv.tv_usec * TICRATE) / 1000000);

  /* Fix for time problem */
  if (!basetime) 
  {
    basetime = thistimereply; thistimereply = 0;
  } else thistimereply -= basetime;

  if (thistimereply < lasttimereply)
    thistimereply = lasttimereply;

  return (lasttimereply = thistimereply);
}

/*
 * I_GetRandomTimeSeed
 *
 * CPhipps - extracted from G_ReloadDefaults because it is O/S based
 */
unsigned long I_GetRandomTimeSeed(void)
{                            
  /* killough 3/26/98: shuffle random seed, use the clock */ 
  struct timeval tv;
  struct timezone tz;
  gettimeofday(&tv,&tz);
  return (tv.tv_sec*1000ul + tv.tv_usec/1000ul);
}

/* cphipps - I_GetVersionString
 * Returns a version string in the given buffer 
 */
const char* I_GetVersionString(char* buf, size_t sz)
{
  sprintf(buf,"PsionDoom v%s",VERSION);
  return buf;
}

/* cphipps - I_SigString
 * Returns a string describing a signal number
 */
const char* I_SigString(char* buf, size_t sz, int signum)
{
#ifdef SYS_SIGLIST_DECLARED
  if (strlen(sys_siglist[signum]) < sz)
    strcpy(buf,sys_siglist[signum]);
  else
#endif
    sprintf(buf,"signal %d",signum);
  return buf;
}

/*
 * HasTrailingSlash
 *
 * cphipps - simple test for trailing slash on dir names
 */

boolean HasTrailingSlash(const char* dn)
{
  return ( (dn[strlen(dn)-1] == '/'));
}

/*
 * I_FindFile
 *
 * proff_fs 2002-07-04 - moved to i_system
 *
 * cphipps 19/1999 - writen to unify the logic in FindIWADFile and the WAD
 *      autoloading code.
 * Searches the standard dirs for a named WAD file
 * The dirs are listed at the start of the function
 */

char* I_FindFile(const char* wfname, const char* ext)
{
  const char* search[] = 
  {
    "C:\\Doom\\",
	"D:\\Doom\\",
  };

  int   i;
  /* Precalculate a length we will need in the loop */
  size_t  pl = strlen(wfname) + strlen(ext) + 4;

  for (i = 0; i < sizeof(search)/sizeof(*search); i++) 
  {
    char  * p = NULL;
    const char  * d = NULL;

	d = search[i];

    p = malloc(strlen(d) + pl);

    sprintf(p, "%s%s", d, wfname);

    if (access(p,F_OK))
      strcat(p, ext);
    if (!access(p,F_OK)) {
      lprintf(LO_INFO, " found %s\n", p);
      return p;
    }

    free(p);
  }
  return NULL;
}


// Return the path where the executable lies -- Lee Killough

const char *I_DoomExeDir(void)
{
	return "D:\\Doom\\";
}


/* 
 * I_Read
 *
 * cph 2001/11/18 - wrapper for read(2) which handles partial reads and aborts
 * on error.
 */
void I_Read(int fd, void* vbuf, size_t sz)
{
  unsigned char* buf = vbuf;

  while (sz) {
    int rc = read(fd,buf,sz);
    if (rc <= 0) {
      I_Error("I_Read: read failed: %s", rc ? strerror(errno) : "EOF");
    }
    sz -= rc; buf += rc;
  }
}


/*
 * I_Filelength
 *
 * Return length of an open file.
 */

int I_Filelength(int handle)
{
  struct stat   fileinfo;
  if (fstat(handle,&fileinfo) == -1)
    I_Error("I_Filelength: %s",strerror(errno));
  return fileinfo.st_size;
}



fixed_t I_GetTimeFrac (void)
{
  unsigned long now;
  fixed_t frac;

  now = 0;

  if (tic_vars.step == 0)
    return FRACUNIT;
  else
  {
    frac = (fixed_t)((now - tic_vars.start + displaytime) * FRACUNIT / tic_vars.step);
    if (frac < 0)
      frac = 0;
    if (frac > FRACUNIT)
      frac = FRACUNIT;
    return frac;
  }
}


void I_GetTime_SaveMS(void)
{
  if (!movement_smooth)
    return;

  tic_vars.start = 0;
  tic_vars.next = (unsigned int) ((tic_vars.start * tic_vars.msec + 1.0f) / tic_vars.msec);
  tic_vars.step = tic_vars.next - tic_vars.start;
}


void I_SetAffinityMask(void)
{
	//Heh!
}