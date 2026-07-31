/*
lib_posix.c - dynamic library code for POSIX systems
Copyright (C) 2018 Flying With Gauss

This program is free software: you can redistribute it and/sor modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "platform/platform.h"
#if XASH_LIB == LIB_POSIX

#if XASH_NSWITCH
	#define SOLDER_LIBDL_COMPAT
	#include <solder.h>
#elif XASH_PSVITA
	#define VRTLD_LIBDL_COMPAT
	#include <vrtld.h>
#else
	#include <dlfcn.h>
#endif

#ifdef XASH_IRIX
#include "platform/irix/dladdr.h"
#endif
#include "common.h"
#include "library.h"
#include "filesystem.h"
#include "server.h"
#if XASH_APPLE
#include "whereami.h" // wai_getExecutablePath: resolve engine dylibs next to the
                      // executable when launched from a .app (cwd = "/").
                      // oldmac fix (include), rev 1.
#endif
#include "platform/android/lib_android.h"

#ifdef XASH_NO_LIBDL
void *dlsym( void *handle, const char *symbol )
{
	Con_DPrintf( "%s( %p, \"%s\" ): stub\n", __func__, handle, symbol );
	return NULL;
}

void *dlopen( const char *name, int flag )
{
	Con_DPrintf( "%s( \"%s\", %d ): stub\n", __func__, name, flag );
	return NULL;
}

int dlclose( void *handle )
{
	Con_DPrintf( "%s( %p ): stub\n", __func__, handle );
	return 0;
}

char *dlerror( void )
{
	return "Loading ELF libraries not supported in this build!\n";
}

int dladdr( const void *addr, Dl_info *info )
{
	return 0;
}
#endif // XASH_NO_LIBDL

qboolean COM_CheckLibraryDirectDependency( const char *name, const char *depname, qboolean directpath )
{
	// TODO: implement
	return true;
}

void *COM_LoadLibrary( const char *dllname, int build_ordinals_table, qboolean directpath )
{
	COM_ResetLibraryError();

	// platforms where gameinfo mechanism is impossible
#ifdef Platform_POSIX_LoadLibrary
	return Platform_POSIX_LoadLibrary( dllname );
#endif

	// platforms where gameinfo mechanism is working goes here
	// and use FS_FindLibrary
	dll_user_t *hInst = FS_FindLibrary( dllname, directpath );
	char buf[MAX_VA_STRING];

	if( !hInst )
	{
		// try to find by linker(LD_LIBRARY_PATH, DYLD_LIBRARY_PATH, LD_32_LIBRARY_PATH and so on...)
		void *pHandle = dlopen( dllname, RTLD_NOW );
		if( pHandle )
			return pHandle;

#if XASH_APPLE
		// A Finder-launched .app runs with cwd = "/", and dyld never resolves a bare
		// leaf name against the cwd. Retry next to the executable. (oldmac .app fix (bare), rev 1.)
		{
			char exepath[MAX_SYSPATH], fullpath[MAX_SYSPATH];
			int dirlen = 0;
			int exelen = wai_getExecutablePath( exepath, sizeof( exepath ) - 1, &dirlen );

			if( exelen > 0 && dirlen > 0 && dirlen < (int)sizeof( exepath ))
			{
				exepath[dirlen] = '\0';
				Q_snprintf( fullpath, sizeof( fullpath ), "%s/%s", exepath, COM_FileWithoutPath( dllname ));
				void *pHandleApple = dlopen( fullpath, RTLD_NOW );
				if( pHandleApple )
					return pHandleApple;
			}
		}
#endif
		Q_snprintf( buf, sizeof( buf ), "Failed to find library %s", dllname );
		COM_PushLibraryError( buf );
		COM_PushLibraryError( dlerror() );
		return NULL;
	}

	if( hInst->custom_loader )
	{
		Q_snprintf( buf, sizeof( buf ), "Custom library loader is not available. Extract library %s and fix gameinfo.txt!", hInst->fullPath );
		COM_PushLibraryError( buf );
		Mem_Free( hInst );
		return NULL;
	}

	hInst->hInstance = dlopen( hInst->fullPath, RTLD_NOW );
#if XASH_APPLE
	if( !hInst->hInstance )
	{
		// FS_FindLibrary hands back a bare leaf name before the filesystem is up
		// (filesystem_stdio), which dyld can't resolve from a Finder .app (cwd="/").
		// Retry next to the executable. (oldmac .app fix (resolved), rev 1.)
		char exepath[MAX_SYSPATH], fullpath[MAX_SYSPATH];
		int dirlen = 0;
		int exelen = wai_getExecutablePath( exepath, sizeof( exepath ) - 1, &dirlen );
		if( exelen > 0 && dirlen > 0 && dirlen < (int)sizeof( exepath ))
		{
			exepath[dirlen] = '\0';
			Q_snprintf( fullpath, sizeof( fullpath ), "%s/%s", exepath, COM_FileWithoutPath( hInst->fullPath ));
			hInst->hInstance = dlopen( fullpath, RTLD_NOW );
		}
	}
#endif
	if( !hInst->hInstance )
	{
		COM_PushLibraryError( dlerror() );
		Mem_Free( hInst );
		return NULL;
	}

	void *pHandle = hInst->hInstance;

	Mem_Free( hInst );

	return pHandle;
}

void COM_FreeLibrary( void *hInstance )
{
#ifdef Platform_POSIX_FreeLibrary
	Platform_POSIX_FreeLibrary( hInstance );
#else
	dlclose( hInstance );
#endif
}

void *COM_GetProcAddress( void *hInstance, const char *name )
{
#if Platform_POSIX_GetProcAddress
	return Platform_POSIX_GetProcAddress( hInstance, name );
#else
	return dlsym( hInstance, name );
#endif
}

void *COM_FunctionFromName( void *hInstance, const char *pName )
{
	return COM_GetProcAddress( hInstance, pName );
}

const char *COM_NameForFunction( void *hInstance, void *function )
{
	// NOTE: dladdr() is a glibc extension
	Dl_info info = {0};
	int ret = dladdr( (void*)function, &info );
	if( ret && info.dli_sname )
	{
		const char *name = info.dli_sname; // oldmac-macho-underscore (task#41, rev 1)
		// Mach-O prepends '_' to symbols; on 10.3 dladdr returns it ("__ZN...").
		// Normalize to the ELF-style "_ZN..." that COM_DetectMangleType expects
		// so save/restore of function pointers round-trips. No-op elsewhere.
		if( name[0] == '_' && name[1] == '_' && name[2] == 'Z' )
			name++;
		return COM_GetPlatformNeutralName( name );
	}

#ifdef XASH_ALLOW_SAVERESTORE_OFFSETS
	return COM_OffsetNameForFunction( function );
#else
	return NULL;
#endif
}

#endif // _WIN32
