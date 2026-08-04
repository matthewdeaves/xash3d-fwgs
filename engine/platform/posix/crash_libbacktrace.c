/*
crash_libbacktrace.c - advanced crashhandler based on libbacktrace
Copyright (C) 2016 Mittorn
Copyright (C) 2025 Alibek Omarov

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#if HAVE_LIBBACKTRACE
#include <signal.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#if XASH_APPLE
#include <sys/ucontext.h>
#endif
#include "common.h"
#include "backtrace.h"
#include "input.h"
#include "crash.h"

static struct backtrace_state *g_bt_state;
static qboolean enable_libbacktrace;

static void Sys_BacktraceError( void *data, const char *msg, int errnum )
{
	Con_Printf( S_ERROR "libbacktrace: %s (%d)\n", msg, errnum );

	if( errnum > 0 )
		enable_libbacktrace = false;
}

struct print_data
{
	char *message;
	size_t message_size;
	int len;
	int idx;
	int logfd;
	qboolean skip_wrappers;
};

static qboolean Sys_IsCrashHandlerFrame( const char *name )
{
	if( !name )
		return false;
	return !Q_strcmp( name, "Sys_Crash" ) || !Q_strcmp( name, "Sys_CrashDetailsLibbacktrace" );
}

static void Sys_AppendPrint( struct print_data *pd, const char *fmt, ... )
{
	va_list va;
	va_start( va, fmt );
	int len = Q_vsnprintf( pd->message, pd->message_size, fmt, va );
	va_end( va );

	if( len > 0 )
	{
		ssize_t unused;
		if( pd->logfd >= 0 )
			unused = write( pd->logfd, pd->message, len );

		unused = write( STDERR_FILENO, pd->message, len );
		(void)unused;

		pd->message += len;
		pd->len += len;
		pd->message_size -= len;
	}
}

static void Sys_BacktracePrintError( void *data, const char *msg, int errnum )
{
	struct print_data *pd = data;
	Sys_AppendPrint( pd, "%2d: error: %s (%d)\n", pd->idx++, msg, errnum );
}

static void Sys_BacktracePrintSyminfo( void *data, uintptr_t pc, const char *symname, uintptr_t symval, uintptr_t symsize )
{
	struct print_data *pd = data;
	Dl_info dlinfo = { 0 };
	const char *module_name;

	if( pd->skip_wrappers )
	{
		if( Sys_IsCrashHandlerFrame( symname ))
			return;
		pd->skip_wrappers = false;
	}

	if( dladdr((void *)pc, &dlinfo ))
		module_name = dlinfo.dli_fname;
	else module_name = NULL;

	if( symname )
	{
		if( module_name )
			Sys_AppendPrint( pd, "%2d: <%s+%d> (%s)\n", pd->idx++, symname, pc - symval, module_name );
		else
			Sys_AppendPrint( pd, "%2d: <%s+%d>\n", pd->idx++, symname, pc - symval );
	}
	else
	{
		// oldmac: dladdr needs no debug info, which is the whole point: these
		// builds carry no -g and no dSYM, so this is the path every frame
		// takes. Print the module and the offset INTO it, because that pair is
		// what atos resolves exactly against a build of the same commit. The
		// symbol is the nearest exported one, so it names the neighbourhood
		// rather than the exact static function.
		if( module_name )
		{
			const char *base = COM_FileWithoutPath( module_name );
			unsigned long off = (unsigned long)( pc - (uintptr_t)dlinfo.dli_fbase );

			if( dlinfo.dli_sname )
				Sys_AppendPrint( pd, "%2d: %s+0x%lx <%s+%ld> [%p]\n", pd->idx++,
					base, off, dlinfo.dli_sname,
					(long)( pc - (uintptr_t)dlinfo.dli_saddr ), (void *)pc );
			else
				Sys_AppendPrint( pd, "%2d: %s+0x%lx [%p]\n", pd->idx++, base, off,
					(void *)pc );
		}
		else
		{
			Sys_AppendPrint( pd, "%2d: %p\n", pd->idx++, (void *)pc );
		}
	}
}

// Frame-level failures are expected on a stripped build and are handled by
// falling back to dladdr, so they are not worth a line each.
static void Sys_BacktraceErrorSilent( void *data, const char *msg, int errnum )
{
}

static int Sys_BacktracePrintFull( void *data, uintptr_t pc, const char *filename, int lineno, const char *function )
{
	struct print_data *pd = data;
	Dl_info dlinfo = { 0 };
	const char *module_name;

	if( pd->skip_wrappers )
	{
		if( Sys_IsCrashHandlerFrame( function ))
			return 0;
		pd->skip_wrappers = false;
	}

	if( dladdr((void *)pc, &dlinfo ))
		module_name = dlinfo.dli_fname;
	else module_name = NULL;

	if( filename && lineno >= 0 && function )
	{
		filename = COM_FileWithoutPath( filename );

		if( module_name )
			Sys_AppendPrint( pd, "%2d: %s (%s:%d) (%s)\n", pd->idx++, function, filename, lineno, module_name );
		else
			Sys_AppendPrint( pd, "%2d: %s (%s:%d)\n", pd->idx++, function, filename, lineno );
	}
	else
	{
		// oldmac: no backtrace_syminfo here: it failed on every frame of these
		// stripped builds, and it was handed Sys_BacktraceError, the STARTUP
		// callback, which sets enable_libbacktrace = false. One unsymbolised
		// frame therefore switched off the whole facility mid-trace. Go
		// straight to the dladdr path instead.
		Sys_BacktracePrintSyminfo( data, pc, NULL, 0, 0 );
	}

	return 0;
}

// oldmac: every frame, with or without debug info. backtrace_full calls its frame
// callback only for frames it can describe from DWARF, and these builds have
// none, so it reported one error per frame and never called it at all. The
// unwinder was working throughout; only symbolisation failed.
// backtrace_simple hands us every program counter, and each is then resolved
// by debug info if there is any and by dladdr if there is not.
static int Sys_BacktraceFrame( void *data, uintptr_t pc )
{
	struct print_data *pd = data;
	int before = pd->idx;

	backtrace_pcinfo( g_bt_state, pc, Sys_BacktracePrintFull,
		Sys_BacktraceErrorSilent, data );

	if( pd->idx == before )
		Sys_BacktracePrintSyminfo( data, pc, NULL, 0, 0 );

	return 0;
}

#if XASH_APPLE && defined( __ppc__ )
// oldmac: unwind PowerPC from the signal context instead of from here.
//
// Every trace this handler produced on a PowerPC machine was three frames long:
// two inside the handler, then libSystem's _sigtramp, then nothing. That is not
// a symbolisation failure, it is the unwinder stopping dead at the signal
// trampoline. _sigtramp is the frame the kernel synthesises to re-enter user
// code, it is written in assembler and carries no unwind information, so a
// walker that has arrived at it has no way to describe the frame below and
// gives up. Everything interesting is below it: the whole faulting call chain.
//
// The kernel already hands us the answer. The third argument to a SA_SIGINFO
// handler is the ucontext, and its machine state holds the register file as it
// was at the instant of the fault: srr0 is the instruction that faulted, lr is
// where it would have returned to, and r1 is the stack pointer. Seeding the walk
// from those three skips the trampoline entirely.
//
// The walk itself is the PowerPC ABI's back chain, which needs no unwind tables
// at all. Word 0 of a frame is the caller's frame pointer and word 2 (offset 8)
// of THAT frame is the return address the callee saved into it. So the chain is
// just repeated pointer-chasing, and it works on a fully stripped binary.
//
// This is deliberately not used on x86_64: there is no back chain to rely on
// there, frame pointers are routinely omitted, and libbacktrace's own unwinder
// already produces correct traces on that slice.
#define OLDMAC_PPC_UNWIND 1

// Darwin renamed every field of the machine state when it adopted the UNIX 03
// namespace rules, so the 10.3 and 10.4 SDKs spell these srr0/r1/lr and later
// ones spell them __srr0/__r1/__lr. Both are built here.
#if defined( __DARWIN_UNIX03 ) && __DARWIN_UNIX03
#define PPCSS( uc )    ((uc)->uc_mcontext->__ss)
#define PPC_SRR0( ss ) ((ss).__srr0)
#define PPC_LR( ss )   ((ss).__lr)
#define PPC_R1( ss )   ((ss).__r1)
#else
#define PPCSS( uc )    ((uc)->uc_mcontext->ss)
#define PPC_SRR0( ss ) ((ss).srr0)
#define PPC_LR( ss )   ((ss).lr)
#define PPC_R1( ss )   ((ss).r1)
#endif

static int g_devnull = -1;

// Probe a read WITHOUT risking a second fault inside the handler.
//
// The stack we are about to walk is by definition suspect: we are here because
// the program corrupted something. Dereferencing a bad back chain would raise
// SIGSEGV from inside the SIGSEGV handler, which on this platform means an
// immediate silent kill and no trace at all, the exact outcome this code exists
// to prevent. write() reports unreadable memory as EFAULT rather than signalling,
// so it is a safe probe, and it is async-signal-safe. /dev/null is opened once at
// startup because open() in a crash handler is a good way to hang.
static qboolean Sys_PPCReadable( const void *p, size_t n )
{
	if( g_devnull < 0 )
		return false;
	return write( g_devnull, p, n ) == (ssize_t)n;
}

static void Sys_PPCBacktrace( struct print_data *pd, void *context )
{
	const ucontext_t *uc = (const ucontext_t *)context;
	uint32_t sp, prev_sp;
	int depth;

	if( !uc || !uc->uc_mcontext )
		return;

	// The two the context gives directly. srr0 is the faulting instruction
	// itself, which no stack walk can recover, and it is usually the whole
	// answer on its own.
	Sys_BacktracePrintSyminfo( pd, (uintptr_t)PPC_SRR0( PPCSS( uc )), NULL, 0, 0 );

	if( PPC_LR( PPCSS( uc )) != PPC_SRR0( PPCSS( uc )))
		Sys_BacktracePrintSyminfo( pd, (uintptr_t)PPC_LR( PPCSS( uc )), NULL, 0, 0 );

	sp = (uint32_t)PPC_R1( PPCSS( uc ));
	prev_sp = 0;

	// 128 is a limit on damage, not on depth: a corrupted chain that happens to
	// point at readable memory could otherwise loop until the alarm fires.
	for( depth = 0; depth < 128; depth++ )
	{
		uint32_t next, ra;

		// The stack grows down and every frame is 16-byte aligned on this ABI,
		// so a back chain that goes backwards or lands off alignment is
		// corrupt, and following it would print fiction.
		if( sp == 0 || ( sp & 15 ) || sp <= prev_sp )
			break;

		if( !Sys_PPCReadable((const void *)(uintptr_t)sp, sizeof( uint32_t )))
			break;

		next = *(const uint32_t *)(uintptr_t)sp;

		if( next == 0 || !Sys_PPCReadable((const void *)(uintptr_t)( next + 8 ), sizeof( uint32_t )))
			break;

		ra = *(const uint32_t *)(uintptr_t)( next + 8 );

		if( ra != 0 )
			Sys_BacktracePrintSyminfo( pd, (uintptr_t)ra, NULL, 0, 0 );

		prev_sp = sp;
		sp = next;
	}
}
#endif // XASH_APPLE && __ppc__

int Sys_CrashDetailsLibbacktrace( int logfd, char *message, int len, size_t max_len, void *context )
{
	struct print_data pd =
	{
		.message = message + len,
		.message_size = max_len - len,
		.logfd = logfd,
		.len = len,
		.skip_wrappers = true,
	};

	// dladdr is NOT async-signal-safe: it takes the dyld lock
	// and can allocate. Reaching it is the price of a symbolised
	// trace on a stripped build, but a fault taken inside dyld or
	// malloc could deadlock here, and a crash handler that hangs
	// is worse than one that says little. Cap it. SIGALRM's
	// default action ends the process, so the worst case becomes a
	// five second wait rather than a permanent stall, and alarm()
	// itself is async-signal-safe.
	alarm( 5 );

#ifdef OLDMAC_PPC_UNWIND
	// Prefer the context walk where we have one. Fall back to libbacktrace only
	// if there is no context, which happens when this is called from somewhere
	// other than a signal handler.
	if( context )
		Sys_PPCBacktrace( &pd, context );
	else
#endif
	backtrace_simple( g_bt_state, 0, Sys_BacktraceFrame,
		Sys_BacktracePrintError, &pd );

	alarm( 0 );

	return pd.len;
}

// oldmac: retry unthreaded: libbacktrace refuses threaded mode unless it was
// configured with HAVE_SYNC_FUNCTIONS, which the gcc-4.0 slices never get
// because gcc-4.0 has no __sync builtins. Swallow that first refusal so the
// retry is not announced; a real failure is still reported by the second.
static void Sys_BacktraceErrorQuiet( void *data, const char *msg, int errnum )
{
}

qboolean Sys_SetupLibbacktrace( const char *argv0 )
{
#ifdef OLDMAC_PPC_UNWIND
	// Opened here, never in the handler: open() can block and can allocate, and
	// a crash handler that hangs tells you less than one that says nothing.
	if( g_devnull < 0 )
		g_devnull = open( "/dev/null", O_WRONLY );
#endif

	enable_libbacktrace = true;
	g_bt_state = backtrace_create_state( argv0, true, Sys_BacktraceErrorQuiet, NULL );

	if( g_bt_state == NULL )
	{
		// The state is written once here at startup and thereafter only read,
		// from a fatal signal handler, so it is never used from two threads at
		// once and unthreaded is the accurate answer rather than a concession.
		enable_libbacktrace = true;
		g_bt_state = backtrace_create_state( argv0, false, Sys_BacktraceError, NULL );
	}

	return g_bt_state != NULL && enable_libbacktrace;
}

#endif // HAVE_LIBBACKTRACE
