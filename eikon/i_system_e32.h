// PsionDoomApp.h
//
// Copyright 17/02/2019 
//

#ifndef HEADER_ISYSTEME32
#define HEADER_ISYSTEME32


#ifdef __cplusplus
extern "C" {
#endif

#include "doomdef.h"
#include "d_event.h"
#include "d_main.h"


void I_InitScreen_e32();

void I_CreateBackBuffer_e32();

int I_GetVideoWidth_e32();

int I_GetVideoHeight_e32();

void I_FinishUpdate_e32(byte* srcBuffer, byte* pallete, const unsigned int width, const unsigned int height);

void I_ProcessKeyEvents();

void I_Error (char *error, ...);

void I_Quit_e32();

#ifdef __cplusplus
}
#endif


#endif