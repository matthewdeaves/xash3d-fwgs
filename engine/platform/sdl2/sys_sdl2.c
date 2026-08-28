/*
sys_sdl.c - SDL2 system utils
Copyright (C) 2018 a1batross

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include <SDL.h>
#include "platform.h"
#include "platform_sdl2.h"

#if XASH_APPLE
#include <sys/utsname.h>   // SDLash_PreflightPermissions gates on the Darwin major
#include <stdlib.h>        // atoi
#endif

#if XASH_TIMER == TIMER_SDL
double Platform_DoubleTime( void )
{
	static Uint64 g_PerformanceFrequency;
	static Uint64 g_ClockStart;

	if( !g_PerformanceFrequency )
	{
		g_PerformanceFrequency = SDL_GetPerformanceFrequency();
		g_ClockStart = SDL_GetPerformanceCounter();
	}
	Uint64 CurrentTime = SDL_GetPerformanceCounter();
	return (double)( CurrentTime - g_ClockStart ) / (double)( g_PerformanceFrequency );
}

void Platform_Sleep( int msec )
{
	SDL_Delay( msec );
}
#endif // XASH_TIMER == TIMER_SDL

#if XASH_MESSAGEBOX == MSGBOX_SDL
void Platform_MessageBox( const char *title, const char *message, qboolean parentMainWindow )
{
	SDL_ShowSimpleMessageBox( SDL_MESSAGEBOX_ERROR, title, message, parentMainWindow ? host.hWnd : NULL );
}
#endif // XASH_MESSAGEBOX == MSGBOX_SDL

static const char *SDLash_CategoryToString( int category )
{
	switch( category )
	{
	case SDL_LOG_CATEGORY_APPLICATION: return "App";
	case SDL_LOG_CATEGORY_ERROR: return "Error";
	case SDL_LOG_CATEGORY_ASSERT: return "Assert";
	case SDL_LOG_CATEGORY_SYSTEM: return "System";
	case SDL_LOG_CATEGORY_AUDIO: return "Audio";
	case SDL_LOG_CATEGORY_VIDEO: return "Video";
	case SDL_LOG_CATEGORY_RENDER: return "Render";
	case SDL_LOG_CATEGORY_INPUT: return "Input";
	case SDL_LOG_CATEGORY_TEST: return "Test";
	default: return "Unknown";
	}
}

static void SDLCALL SDLash_LogOutputFunction( void *userdata, int category, SDL_LogPriority priority, const char *message )
{
	const char *str = "";

	switch( priority )
	{
	case SDL_LOG_PRIORITY_CRITICAL:
	case SDL_LOG_PRIORITY_ERROR:
		str = S_ERROR;
		break;
	case SDL_LOG_PRIORITY_WARN:
		str = S_WARN;
		break;
	case SDL_LOG_PRIORITY_INFO:
		str = S_NOTE;
		break;
	default:
		str = "";
		break;
	}

	Con_Reportf( "%s" S_BLUE "SDL" S_DEFAULT ": [%s] %s\n", str, SDLash_CategoryToString( category ), message );
}

#if XASH_APPLE
/*
=============
SDLash_PreflightPermissions

oldmac: ask macOS for the microphone HERE, at launch, while this process still
has no window.

The engine does not open the microphone at startup: cl_main.c calls Voice_Init
with preinit = true, which registers the codec and touches no device. The device
is opened later, from cl_parse.c, when the SERVER announces its voice codec, and
that runs on to VoiceCapture_Init -> SDL_OpenAudioDevice( NULL, SDL_TRUE, ... ).

So on macOS 10.14 and later the permission prompt lands at connect time, with
the game fullscreen and the mouse grabbed, and the player cannot answer it.
Reported on imac-2019 (15.7.9) twice on 2026-08-28: the prompt appeared and
could not be clicked, which blocks joining a server. NSMicrophoneUsageDescription
is present and correct in the Info.plist, so macOS prompts rather than killing
the process; the key was never the problem, the timing was.

Opening and immediately closing a capture device here triggers the same prompt
from a windowless process during startup, where it is reachable. Whatever the
player answers is remembered by the system, so the connect-time open later finds
the decision already made and never prompts again. Deliberately ignores the
result: this is not a capability check, and a machine with no microphone at all
must still start normally.

Uses SDL rather than AVCaptureDevice on purpose. The authorization API is 10.14+
and is not in the 10.7 SDK this fork builds the Intel slice against, so it could
not be called from that slice without runtime symbol lookup. SDL is already
linked and its coreaudio backend triggers the same TCC check.

Gated at RUNTIME on Darwin major >= 18 (macOS 10.14), not at compile time: one
x86_64 binary serves 10.6.8 through macOS 26, so the same code ships to machines
that have TCC and machines that have never heard of it. Below 18 this does
nothing at all, which keeps every PowerPC machine and both Lion minis on exactly
the path they had before. Issue #25.
=============
*/
static void SDLash_PreflightPermissions( void )
{
	struct utsname u;
	SDL_AudioSpec wanted, got;
	SDL_AudioDeviceID dev;

	if( Host_IsDedicated( ))
		return; // a dedicated server never captures audio

	if( uname( &u ) != 0 || atoi( u.release ) < 18 )
		return; // pre-10.14: no TCC, nothing to ask for

	if( SDL_InitSubSystem( SDL_INIT_AUDIO ) != 0 )
		return;

	SDL_zero( wanted );
	wanted.freq = 44100;
	wanted.format = AUDIO_S16SYS;
	wanted.channels = 1;
	wanted.samples = 1024;

	dev = SDL_OpenAudioDevice( NULL, SDL_TRUE, &wanted, &got, 0 );

	if( dev != 0 )
		SDL_CloseAudioDevice( dev );

	// Leave SDL_INIT_AUDIO up: Sound_Init wants it shortly anyway, and quitting
	// the subsystem here would tear down state it is about to build.
}
#endif // XASH_APPLE

void SDLash_Init( void )
{
#if XASH_IOS
	char *path = SDL_GetBasePath();
	if( path != NULL )
	{
		char buf[MAX_VA_STRING];

		Q_snprintf( buf, sizeof( buf ), "%s%s/extras.pk3", path, host.default_gamedir );
		setenv( "XASH3D_EXTRAS_PAK1", buf, true );
	}
#endif

	SDL_LogSetOutputFunction( SDLash_LogOutputFunction, NULL );

	if( host_developer.value >= 2 )
		SDL_LogSetAllPriority( SDL_LOG_PRIORITY_VERBOSE );
	else if( host_developer.value >= 1 )
		SDL_LogSetAllPriority( SDL_LOG_PRIORITY_WARN );
	else
		SDL_LogSetAllPriority( SDL_LOG_PRIORITY_ERROR );

#if XASH_WIN32
#ifdef SDL_HINT_WINDOWS_DPI_AWARENESS
	SDL_SetHint( SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitor" );
#endif
	// TODO: disabled for now
	// try to test it better when we'll come back to highdpi support issue
	// SDL_SetHint( SDL_HINT_WINDOWS_DPI_SCALING, "1" );
#endif // XASH_WIN32

#ifdef SDL_HINT_ANDROID_BLOCK_ON_PAUSE
	SDL_SetHint( SDL_HINT_ANDROID_BLOCK_ON_PAUSE, "0" );
#endif
#ifdef SDL_HINT_ANDROID_BLOCK_ON_PAUSE_PAUSEAUDIO
	SDL_SetHint( SDL_HINT_ANDROID_BLOCK_ON_PAUSE_PAUSEAUDIO, "0" );
#endif

	// when launched through Steam (notably on Steam Deck) Steam Input hides the
	// real controller and exposes a virtual gamepad without gyro/touchpad access
	// undo the env-var filter and ignore the virtual pad instead
	if( Sys_CheckParm( "-nosteaminput" ))
	{
		SDL_setenv( "SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT", "", 1 );
		SDL_setenv( "SDL_GAMECONTROLLER_IGNORE_DEVICES", "0x28DE/0x11FF", 1 );
	}

	if( SDL_Init( SDL_INIT_TIMER | SDL_INIT_VIDEO | SDL_INIT_EVENTS ) )
	{
		Sys_Warn( "SDL_Init failed: %s", SDL_GetError() );
		host.type = HOST_DEDICATED;
	}

#ifdef SDL_HINT_ACCELEROMETER_AS_JOYSTICK
	SDL_SetHint( SDL_HINT_ACCELEROMETER_AS_JOYSTICK, "0" );
#endif
#ifdef SDL_HINT_JOYSTICK_HIDAPI_STEAM
	SDL_SetHint( SDL_HINT_JOYSTICK_HIDAPI_STEAM, "1" );
#endif
#ifdef SDL_HINT_ANDROID_TRAP_BACK_BUTTON
	SDL_SetHint( SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1" );
#endif
#ifdef SDL_HINT_ORIENTATIONS
	SDL_SetHint( SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight" );
#endif

#ifdef SDL_HINT_MOUSE_TOUCH_EVENTS
	SDL_SetHint( SDL_HINT_MOUSE_TOUCH_EVENTS, "0" );
#endif // SDL_HINT_MOUSE_TOUCH_EVENTS
#ifdef SDL_HINT_TOUCH_MOUSE_EVENTS
	SDL_SetHint( SDL_HINT_TOUCH_MOUSE_EVENTS, "0" );
#endif

	// NOTE: setting this hint makes no sense, as of course
	// it doesn't make warps magically work in normal, non-relative mode
	// there is pointer_warp_v1 extension but it's only implemented in SDL3
	// at the time of writing, so there it should just work if compositor
	// supports it

	// SDL_SetHint( SDL_HINT_VIDEO_WAYLAND_EMULATE_MOUSE_WARP, "1" );

	SDL_StopTextInput();

	SDLash_InitCursors();
	SDLash_InitSensors();

#if XASH_APPLE
	SDLash_PreflightPermissions();
#endif
}

void SDLash_Shutdown( void )
{
	SDLash_ShutdownSensors();
	SDLash_FreeCursors();

	SDL_Quit();
}
