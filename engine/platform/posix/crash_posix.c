/*
crash_posix.c - advanced crashhandler
Copyright (C) 2016 Mittorn

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "common.h"

#if XASH_FREEBSD || XASH_NETBSD || XASH_OPENBSD || XASH_ANDROID || XASH_LINUX || XASH_APPLE
#include <signal.h>
#include <sys/mman.h>
#if XASH_ANDROID
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <android/log.h>
#endif
#include "library.h"
#include "input.h"
#include "crash.h"

#if XASH_ANDROID
static char crashlog_path[MAX_OSPATH];
static char enginelog_path[MAX_OSPATH];
#endif

static qboolean have_libbacktrace = false;
static char crash_message[8192];

#if XASH_APPLE
// oldmac: last-resort exit if Platform_MessageBox's Cocoa call deadlocks
// (documented above, at the call site). SIGALRM firing means the 5s timeout
// elapsed with the dialog call never returning; _exit is async-signal-safe
// and unconditional, so this always gets the process (and the machine) back
// even in the worst case. See issue #18.
static void Sys_CrashDialogWatchdog( int sig )
{
	_exit( 128 );
}

// oldmac: a second crash on another thread while the first is still trying
// to show its dialog hits the exact same deadlock trying to show ITS OWN -
// measured live, two threads stuck in the identical call chain at once. Only
// the first crash gets to attempt the (watchdog-guarded) dialog; anything
// after that exits immediately rather than piling up more stuck threads.
// sig_atomic_t + a simple flag, not a real lock: this runs from a signal
// handler, where a mutex is itself unsafe to take.
static volatile sig_atomic_t crash_in_progress = 0;
#endif

static void Sys_Crash( int signal, siginfo_t *si, void *context )
{
#if XASH_APPLE
	if( crash_in_progress )
		_exit( 128 + signal );
	crash_in_progress = 1;
#endif

	// safe actions first, stack and memory may be corrupted
	int len = Q_snprintf( crash_message, sizeof( crash_message ), "Ver: " XASH_ENGINE_NAME " " XASH_VERSION " (build %i-%s-%s, %s-%s)\n",
		Q_buildnum(), g_buildcommit, g_buildbranch, Q_buildos(), Q_buildarch() );

#if !XASH_FREEBSD && !XASH_NETBSD && !XASH_OPENBSD && !XASH_APPLE
	len += Q_snprintf( crash_message + len, sizeof( crash_message ) - len, "Crash: signal %d errno %d with code %d at %p %p\n", signal, si->si_errno, si->si_code, si->si_addr, si->si_ptr );
#else
	len += Q_snprintf( crash_message + len, sizeof( crash_message ) - len, "Crash: signal %d errno %d with code %d at %p\n", signal, si->si_errno, si->si_code, si->si_addr );
#endif

	ssize_t unused = write( STDERR_FILENO, crash_message, len );

#if XASH_ANDROID
	__android_log_write( ANDROID_LOG_FATAL, "Xash", crash_message );
#endif

	// now get log fd and write trace directly to log
	int logfd = Sys_LogFileNo();
	if( logfd >= 0 )
		unused = write( logfd, crash_message, len );
	(void)unused;

#if HAVE_LIBBACKTRACE
	qboolean detailed_message = false;
	if( have_libbacktrace && !detailed_message )
	{
		len = Sys_CrashDetailsLibbacktrace( logfd, crash_message, len, sizeof( crash_message ), context );
		detailed_message = true;
	}
#endif // HAVE_LIBBACKTRACE

#if XASH_ANDROID
	// also write to a dedicated crash report file the Java side picks up on next launch
	if( crashlog_path[0] )
	{
		int crashfd = open( crashlog_path, O_WRONLY|O_CREAT|O_TRUNC, 0644 );
		if( crashfd >= 0 )
		{
			write( crashfd, crash_message, len );
			close( crashfd );
		}
	}

	// make a copy of engine.log in staging directory
	// TODO: dump log from console buffers, if -log not enabled
	if( logfd >= 0 && enginelog_path[0] && lseek( logfd, 0, SEEK_SET ) == 0 )
	{
		int outfd = open( enginelog_path, O_WRONLY|O_CREAT|O_TRUNC, 0644 );
		if( outfd >= 0 )
		{
			static char buf[8192];
			while( 1 )
			{
				ssize_t n = read( logfd, buf, sizeof( buf ));
				if( n <= 0 )
					break;
				if( write( outfd, buf, (size_t)n ) != n )
					break;
			}
			close( outfd );
		}
	}

	// JNI/SDL calls aren't safe from a signal handler on Android
	_exit( 128 + signal );
#else
#if !XASH_DEDICATED
	IN_SetMouseGrab( false );
#endif
	host.status = HOST_CRASHED;

	// oldmac: -nomsgbox suppresses this dialog, and only this dialog. The crash
	// text has already gone to stderr and to the log by the time we get here, so
	// nothing is lost by not drawing it.
	//
	// It exists for the bench machines. This box is modal, so a crash during an
	// automated run leaves the engine parked on a dialog that nobody is sitting
	// in front of, holding the machine until a person walks over and clicks it.
	// The run's watchdog does eventually kill the process, but every crash still
	// costs a human interruption, and a fleet of six is where that adds up.
	//
	// It stays on by default: for somebody actually playing the game, a dialog
	// saying what happened is much better than the window vanishing.
	//
	// oldmac 2026-08-28: Platform_MessageBox is NOT async-signal-safe on Apple -
	// SDL's Cocoa message box allocates memory (NSAutoreleasePool, etc), and
	// malloc is documented as unsafe to call from a signal handler: if the
	// thread that crashed was itself inside malloc when the fault hit, this
	// call deadlocks on the SAME lock, on the SAME thread, forever - no dialog,
	// no log line, nothing recoverable short of a hard kill. Measured live on
	// g5-desktop (10.5.8): a real crash inside libmenu.dylib produced exactly
	// this - a system-wide beachball that `killall -TERM` could not touch, and
	// a second, concurrent crash on another thread hit the identical deadlock
	// trying to show ITS OWN dialog at the same time (sample(1): two threads
	// both spinning in szone_malloc/__spin_lock under
	// SDL_ShowSimpleMessageBox -> Cocoa_RegisterApp -> TransformProcessType).
	// The crash text is already safely on disk (write() above is signal-safe)
	// by the time we get here, so nothing is lost by risking this. A watchdog
	// alarm guarantees the process exits either way: normally the dialog shows
	// and Sys_Quit runs well inside the timeout; if the deadlock happens, the
	// alarm fires and force-exits instead of hanging the whole session. See
	// issue #18.
#if XASH_APPLE
	// sigaction, not signal(): Sys_Crash's own `signal` PARAMETER shadows the
	// libc signal() function name in this scope.
	struct sigaction alarm_act = { .sa_handler = Sys_CrashDialogWatchdog };
	sigaction( SIGALRM, &alarm_act, NULL );
	alarm( 5 );
#endif
	if( !Sys_CheckParm( "-nomsgbox" ))
		Platform_MessageBox( "Xash Error", crash_message, false );
#if XASH_APPLE
	alarm( 0 );
#endif

	// log saved, now we can try to save configs and close log correctly, it may crash
	if( host.type == HOST_NORMAL )
		CL_Crashed();

	Sys_Quit( "crashed" );
#endif // XASH_ANDROID
}

static struct sigaction old_segv_act;
static struct sigaction old_abrt_act;
static struct sigaction old_bus_act;
static struct sigaction old_ill_act;

void Sys_SetupCrashHandler( const char *argv0 )
{
	struct sigaction act =
	{
		.sa_sigaction = Sys_Crash,
		.sa_flags = SA_SIGINFO | SA_ONSTACK,
	};

#if XASH_ANDROID
	const char *crashdir = getenv( "XASH3D_CRASH_DIR" );

	if( !COM_StringEmptyOrNULL( crashdir ))
	{
		Q_snprintf( crashlog_path, sizeof( crashlog_path ), "%s/crash.log", crashdir );
		Q_snprintf( enginelog_path, sizeof( enginelog_path ), "%s/engine.log", crashdir );
	}

	// unblock the engine/SDL_main thread just in case
	sigset_t set;
	sigemptyset( &set );
	sigaddset( &set, SIGSEGV );
	sigaddset( &set, SIGABRT );
	sigaddset( &set, SIGBUS );
	sigaddset( &set, SIGILL );
	pthread_sigmask( SIG_UNBLOCK, &set, NULL );
#endif

#if HAVE_LIBBACKTRACE
	have_libbacktrace = Sys_SetupLibbacktrace( argv0 );
#endif // HAVE_LIBBACKTRACE

	sigaction( SIGSEGV, &act, &old_segv_act );
	sigaction( SIGABRT, &act, &old_abrt_act );
	sigaction( SIGBUS,  &act, &old_bus_act );
	sigaction( SIGILL,  &act, &old_ill_act );
}

void Sys_RestoreCrashHandler( void )
{
	sigaction( SIGSEGV, &old_segv_act, NULL );
	sigaction( SIGABRT, &old_abrt_act, NULL );
	sigaction( SIGBUS,  &old_bus_act, NULL );
	sigaction( SIGILL,  &old_ill_act, NULL );
}

#endif // XASH_FREEBSD || XASH_NETBSD || XASH_OPENBSD || XASH_ANDROID || XASH_LINUX
