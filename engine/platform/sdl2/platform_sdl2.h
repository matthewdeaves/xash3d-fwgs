/*
platform_sdl2.h - SDL backend internal header
Copyright (C) 2015-2018 a1batross

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#pragma once
#ifndef KEYWRAPPER_H
#define KEYWRAPPER_H
#ifdef  XASH_SDL

#include "platform.h"

// window management
void VID_RestoreScreenResolution( window_mode_t window_mode );
void VID_SaveWindowSize( int width, int height );

//
// in_sdl.c
//
void SDLash_InitCursors( void );
void SDLash_FreeCursors( void );

//
// joy_sdl.c
//
void SDLash_HandleGameControllerEvent( SDL_Event *ev );

//
// in_sdl2.c
//
// oldmac: does this OS actually deliver SDL_TEXTINPUT events? False on macOS
// before 10.5, where SDL's Cocoa text input produces no text and stalls the
// menu. Both host_sdl2.c and in_sdl2.c need it. See GitHub #29.
qboolean SDLash_TextInputDelivers( void );

//
// sensor_sdl2.c
//
void SDLash_InitSensors( void );
void SDLash_ShutdownSensors( void );
qboolean SDLash_GyroIsAvailable( void );
#if SDL_VERSION_ATLEAST( 2, 0, 14 )
void SDLash_SensorUpdate( SDL_SensorEvent sensor );
#endif

#endif // XASH_SDL
#endif // KEYWRAPPER_H
