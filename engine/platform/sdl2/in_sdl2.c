/*
in_sdl.c - SDL input component
Copyright (C) 2018-2025 a1batross

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

#include "common.h"
#include "keydefs.h"
#include "input.h"
#include "client.h"
#include "vgui_draw.h"
#include "platform_sdl2.h"
#include "sound.h"
#include "vid_common.h"

static struct
{
	qboolean initialized;
	SDL_Cursor *cursors[dc_last];
} cursors;

static struct
{
	int x, y;
	qboolean pushed;
} in_visible_cursor_pos;

/*
=============
Platform_GetMousePos

=============
*/
void GAME_EXPORT Platform_GetMousePos( int *x, int *y )
{
	SDL_GetMouseState( x, y );

	if( x )
		*x *= refState.scale_x;

	if( y )
		*y *= refState.scale_y;
}

/*
=============
Platform_SetMousePos

============
*/
void GAME_EXPORT Platform_SetMousePos( int x, int y )
{
	SDL_WarpMouseInWindow( host.hWnd, x, y );
}

/*
========================
Platform_MouseMove

========================
*/
void Platform_MouseMove( float *x, float *y )
{
	int m_x, m_y;
	SDL_GetRelativeMouseState( &m_x, &m_y );
	*x = (float)m_x;
	*y = (float)m_y;
}

/*
=============
Platform_GetClipobardText

=============
*/
int Platform_GetClipboardText( char *buffer, size_t size )
{
	char *sdlbuffer = SDL_GetClipboardText();

	if( !sdlbuffer )
		return 0;

	int textLength;
	if (buffer && size > 0)
	{
		textLength = Q_strncpy( buffer, sdlbuffer, size );
	}
	else {
		textLength = Q_strlen( sdlbuffer );
	}
	SDL_free( sdlbuffer );
	return textLength;
}

/*
=============
Platform_SetClipobardText

=============
*/
void Platform_SetClipboardText( const char *buffer )
{
	SDL_SetClipboardText( buffer );
}

#if !XASH_PSVITA

/*
=============
SDLash_EnableTextInput

=============
*/
/*
=============
SDLash_TextInputDelivers

oldmac: does this OS actually deliver SDL_TEXTINPUT events?

This matters because SDLash_KeyEvent in host_sdl2.c DROPS every printable key
while SDL_IsTextInputActive() is true, on the understanding that those
characters will arrive as SDL_TEXTINPUT instead. SDL's Cocoa text input builds
a field editor and adds it as a subview of the GL content view, then makes it
first responder, and routes keys through -interpretKeyEvents: and the process
wide NSInputManager.

This was believed to be an OS-version question (Darwin major <= 8, i.e.
10.3/10.4 only) on the strength of one dated finding: "a G3 on 10.3.9 loses
every character; an iMac G5 on 10.5.8 running the same ppc7400 slice types
fine." That finding was never rechecked against a SECOND G5. Measured
2026-08-28, hands-on on the DUAL PowerMac G5 (`g5-desktop`, 10.5.8, Radeon
9600, this fork's v1.9.8): opening the player-name text box with the
OS-version-gated build (this build STILL used SDL_StartTextInput on Leopard)
produced a SYSTEM-WIDE beachball, not just an unresponsive dialog - worse than
the G3 symptom, and `killall -TERM` did not recover it, only `-KILL` did. So
the iMac G5 finding does not generalise even to every other G5, and "OS
version" was the wrong axis: it happened to correlate with the two machines
that were actually tested (a pre-Leopard G3/G4 and one Leopard iMac G5), not
with the real cause.

Gated on CPU architecture instead, matching how the rest of this fork
distinguishes PowerPC (`vid_common.c`, `gl_opengl.c`): every PowerPC slice
takes the key-derived path, on every OS version, because the flaky part is
SDL's Cocoa field editor integration on PowerPC's older AppKit, not a specific
OS release. Intel and arm64 are untouched - no failure has ever been measured
there, and disabling SDL's real text input there would cost IME/international
keyboard support for nothing.
=============
*/
#if XASH_APPLE
#if defined( __ppc__ ) || defined( __ppc64__ )
qboolean SDLash_TextInputDelivers( void ) { return false; }
#else
qboolean SDLash_TextInputDelivers( void ) { return true; }
#endif
#else
qboolean SDLash_TextInputDelivers( void ) { return true; }
#endif

void Platform_EnableTextInput( qboolean enable )
{
	// oldmac: on PowerPC, do not start SDL's text input at all - see the doc
	// comment on SDLash_TextInputDelivers above for what changed 2026-08-28 and
	// why, and GitHub #29 for the original G3/G4 finding.
	//
	// Skipping it leaves SDL_IsTextInputActive() false, and the key handler in
	// host_sdl2.c then makes characters from the key events themselves, the way
	// the SDL 1.2 backend always has. host.textmode still tracks the engine's
	// intent, so nothing else changes.
	if( !SDLash_TextInputDelivers( ))
		return;

	enable ? SDL_StartTextInput() : SDL_StopTextInput();
}

#endif // !XASH_PSVITA

/*
========================
SDLash_InitCursors

========================
*/
void SDLash_InitCursors( void )
{
	if( cursors.initialized )
		SDLash_FreeCursors();

	// load up all default cursors
	cursors.cursors[dc_none] = NULL;
	cursors.cursors[dc_arrow] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
	cursors.cursors[dc_ibeam] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_IBEAM);
	cursors.cursors[dc_hourglass] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_WAIT);
	cursors.cursors[dc_crosshair] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_CROSSHAIR);
	cursors.cursors[dc_up] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
	cursors.cursors[dc_sizenwse] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZENWSE);
	cursors.cursors[dc_sizenesw] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZENESW);
	cursors.cursors[dc_sizewe] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZEWE);
	cursors.cursors[dc_sizens] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZENS);
	cursors.cursors[dc_sizeall] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZEALL);
	cursors.cursors[dc_no] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_NO);
	cursors.cursors[dc_hand] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);
	cursors.initialized = true;
}

/*
========================
SDLash_FreeCursors

========================
*/
void SDLash_FreeCursors( void )
{
	for( int i = 0; i < ARRAYSIZE( cursors.cursors ); i++ )
	{
		if( cursors.cursors[i] )
			SDL_FreeCursor( cursors.cursors[i] );
		cursors.cursors[i] = NULL;
	}

	cursors.initialized = false;
}

/*
========================
Platform_SetCursorType

========================
*/
void Platform_SetCursorType( VGUI_DefaultCursor type )
{
	qboolean visible;

	switch( type )
	{
		case dc_user:
		case dc_none:
			visible = false;
			break;
		default:
			visible = true;
			break;
	}

	// never disable cursor in touch emulation mode
	if( !visible && Touch_WantVisibleCursor( ))
		return;

	host.mouse_visible = visible;
	VGui_UpdateInternalCursorState( type );

	if( host.mouse_visible )
	{
		if( cursors.initialized )
			SDL_SetCursor( cursors.cursors[type] );

		SDL_ShowCursor( true );

		// restore the last mouse position
		if( in_visible_cursor_pos.pushed )
		{
			Platform_SetMousePos( in_visible_cursor_pos.x, in_visible_cursor_pos.y );
			in_visible_cursor_pos.pushed = false;
		}
	}
	else
	{
		// save last mouse position and warp it to the center
		if( !in_visible_cursor_pos.pushed )
		{
			SDL_GetMouseState( &in_visible_cursor_pos.x, &in_visible_cursor_pos.y );
			Platform_SetMousePos( host.window_center_x, host.window_center_y );
			in_visible_cursor_pos.pushed = true;
		}

		SDL_ShowCursor( false );
	}
}

/*
========================
Platform_GetMouseGrab
========================
*/
qboolean Platform_GetMouseGrab( void )
{
	return SDL_GetWindowGrab( host.hWnd );
}

/*
========================
Platform_SetMouseGrab
========================
*/
void Platform_SetMouseGrab( qboolean enable )
{
	SDL_SetWindowGrab( host.hWnd, enable );
}

/*
========================
Platform_GetKeyModifiers

========================
*/
key_modifier_t Platform_GetKeyModifiers( void )
{
	key_modifier_t resultFlags = KeyModifier_None;
	SDL_Keymod modFlags = SDL_GetModState();
	if( FBitSet( modFlags, KMOD_LCTRL ))
		SetBits( resultFlags, KeyModifier_LeftCtrl );
	if( FBitSet( modFlags, KMOD_RCTRL ))
		SetBits( resultFlags, KeyModifier_RightCtrl );
	if( FBitSet( modFlags, KMOD_RSHIFT ))
		SetBits( resultFlags, KeyModifier_RightShift );
	if( FBitSet( modFlags, KMOD_LSHIFT ))
		SetBits( resultFlags, KeyModifier_LeftShift );
	if( FBitSet( modFlags, KMOD_LALT ))
		SetBits( resultFlags, KeyModifier_LeftAlt );
	if( FBitSet( modFlags, KMOD_RALT ))
		SetBits( resultFlags, KeyModifier_RightAlt );
	if( FBitSet( modFlags, KMOD_NUM ))
		SetBits( resultFlags, KeyModifier_NumLock );
	if( FBitSet( modFlags, KMOD_CAPS ))
		SetBits( resultFlags, KeyModifier_CapsLock );
	if( FBitSet( modFlags, KMOD_RGUI ))
		SetBits( resultFlags, KeyModifier_RightSuper );
	if( FBitSet( modFlags, KMOD_LGUI ))
		SetBits( resultFlags, KeyModifier_LeftSuper );

	return resultFlags;
}
