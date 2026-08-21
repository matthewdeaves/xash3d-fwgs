/*
sv_filter.c - server ID/IP filter
Copyright (C) 2017 a1batross

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
#include "server.h"


/*
=============================================================================

PLAYER ID FILTER

=============================================================================
*/
typedef struct cidfilter_s
{
	float endTime;
	struct cidfilter_s *next;
	string id;
} cidfilter_t;

static cidfilter_t *cidfilter = NULL;

static void SV_RemoveID( const char *id )
{
	cidfilter_t *prevfilter = NULL;

	for( cidfilter_t *filter = cidfilter; filter; filter = filter->next )
	{
		if( Q_strcmp( filter->id, id ))
		{
			prevfilter = filter;
			continue;
		}

		if( filter == cidfilter )
		{
			cidfilter = cidfilter->next;
			Mem_Free( filter );
			return;
		}

		if( prevfilter )
		prevfilter->next = filter->next;
		Mem_Free( filter );
		return;
	}
}

qboolean SV_CheckID( const char *id )
{
	qboolean ret = false;

	for( cidfilter_t *filter = cidfilter; filter; filter = filter->next )
	{
		int len1 = Q_strlen( id ), len2 = Q_strlen( filter->id );
		int len = Q_min( len1, len2 );

		while( filter->endTime && host.realtime > filter->endTime )
		{
			char *fid = filter->id;
			filter = filter->next;
			SV_RemoveID( fid );
			if( !filter )
				return false;
		}

		if( !Q_strncmp( id, filter->id, len ))
		{
			ret = true;
			break;
		}
	}

	return ret;
}

static void SV_BanID_f( void )
{
	float time = Q_atof( Cmd_Argv( 1 ));
	const char *id = Cmd_Argv( 2 );
	sv_client_t *cl = NULL;

	if( time )
		time = host.realtime + time * 60.0f;

	if( !id[0] )
	{
		Con_Reportf( S_USAGE "banid <minutes> <#userid or unique id>\n0 minutes for permanent ban\n" );
		return;
	}

	if( !svs.clients )
	{
		Con_Reportf( S_ERROR "banid: no players\n" );
		return;
	}

	if( id[0] == '#' )
	{
		Con_Printf( S_ERROR "banid: not supported\n" );
		return;
#if 0
		int i = Q_atoi( &id[1] );

		cl = SV_ClientById( i );

		if( !cl )
		{
			Con_Printf( S_ERROR "banid: no such player with userid %d\n", i );
			return;
		}
#endif
	}
	else
	{
		size_t len;
		int i;

		if( !Q_strnicmp( id, "STEAM_", 6 ) || !Q_strnicmp( id, "VALVE_", 6 ))
			id += 6;
		if( !Q_strnicmp( id, "XASH_", 5 ))
			id += 5;

		len = Q_strlen( id );

		for( i = 0; i < svs.maxclients; i++ )
		{
			if( FBitSet( svs.clients[i].flags, FCL_FAKECLIENT ))
				continue;

			if( svs.clients[i].state != cs_spawned )
				continue;

			if( !Q_strncmp( id, Info_ValueForKey( svs.clients[i].useragent, "uuid" ), len ))
			{
				cl = &svs.clients[i];
				break;
			}
		}

		if( !cl )
		{
			Con_Printf( S_ERROR "banid: no such player with userid %s\n", id );
			return;
		}
	}

	id = Info_ValueForKey( cl->useragent, "uuid" );

	SV_RemoveID( id );

	cidfilter_t *filter = Mem_Malloc( host.mempool, sizeof( cidfilter_t ));
	filter->endTime = time;
	filter->next = cidfilter;
	Q_strncpy( filter->id, id, sizeof( filter->id ));
	cidfilter = filter;

	if( cl && !Q_stricmp( Cmd_Argv( Cmd_Argc() - 1 ), "kick" ))
		Cbuf_AddTextf( "kick #%d \"Kicked and banned\"\n", cl->userid );
}

static void SV_ListID_f( void )
{
	Con_Reportf( "id ban list\n" );
	Con_Reportf( "-----------\n" );

	for( cidfilter_t *filter = cidfilter; filter; filter = filter->next )
	{
		if( filter->endTime && host.realtime > filter->endTime )
			continue; // no negative time

		if( filter->endTime )
			Con_Reportf( "%s expries in %f minutes\n", filter->id, ( filter->endTime - host.realtime ) / 60.0f );
		else
			Con_Reportf( "%s permanent\n", filter->id );
	}
}

static void SV_RemoveID_f( void )
{
	const char *id = Cmd_Argv( 1 );

	if( id[0] == '#' && svs.clients )
	{
		int num = Q_atoi( id + 1 );

		if( num >= svs.maxclients || num < 0 )
			return;

		id = Info_ValueForKey( svs.clients[num].useragent, "uuid" );
	}

	if( !id[0] )
	{
		Con_Reportf( S_USAGE "removeid <#slotnumber or uniqueid>\n");
		return;
	}

	SV_RemoveID( id );
}

static void SV_WriteID_f( void )
{
	file_t *f = FS_Open( Cvar_VariableString( "bannedcfgfile" ), "w", false );

	if( !f )
	{
		Con_DPrintf( S_ERROR "Could not write %s\n", Cvar_VariableString( "bannedcfgfile" ));
		return;
	}

	FS_Printf( f, "//=======================================================================\n" );
	FS_Printf( f, "//\t\tCopyright Flying With Gauss Team %s ©\n", Q_timestamp( TIME_YEAR_ONLY ));
	FS_Printf( f, "//\t\t    %s - archive of id blacklist\n", Cvar_VariableString( "bannedcfgfile" ));
	FS_Printf( f, "//=======================================================================\n" );

	for( cidfilter_t *filter = cidfilter; filter; filter = filter->next )
		if( !filter->endTime ) // only permanent
			FS_Printf( f, "banid 0 %s\n", filter->id );

	FS_Close( f );
}

static void SV_InitIDFilter( void )
{
	Cmd_AddRestrictedCommand( "banid", SV_BanID_f, "ban player by ID" );
	Cmd_AddRestrictedCommand( "listid", SV_ListID_f, "list banned players" );
	Cmd_AddRestrictedCommand( "removeid", SV_RemoveID_f, "remove player from banned list" );
	Cmd_AddRestrictedCommand( "writeid", SV_WriteID_f, "write banned.cfg" );
}

static void SV_ShutdownIDFilter( void )
{
	// should be called manually because banned.cfg is not executed by engine
	//SV_WriteID_f();

	Cmd_RemoveCommand( "banid" );
	Cmd_RemoveCommand( "listid" );
	Cmd_RemoveCommand( "removeid" );
	Cmd_RemoveCommand( "writeid" );

	for( cidfilter_t *cidList = cidfilter, *cidNext; cidList; cidList = cidNext )
	{
		cidNext = cidList->next;
		Mem_Free( cidList );
	}

	cidfilter = NULL;
}

/*
=============================================================================

CLIENT IP FILTER

=============================================================================
*/

typedef struct ipfilter_s
{
	float endTime;
	struct ipfilter_s *next;
	netadr_t adr;
	uint prefixlen;
} ipfilter_t;

static ipfilter_t *ipfilter = NULL;

static int SV_FilterToString( char *dest, size_t size, qboolean config, ipfilter_t *f )
{
	if( config )
	{
		return Q_snprintf( dest, size, "addip 0 %s/%d\n", NET_BaseAdrToString( f->adr ), f->prefixlen );
	}
	else if( f->endTime )
	{
		return Q_snprintf( dest, size, "%s/%d (%f minutes)", NET_BaseAdrToString( f->adr ), f->prefixlen, f->endTime );
	}

	return Q_snprintf( dest, size, "%s/%d (permanent)", NET_BaseAdrToString( f->adr ), f->prefixlen );
}

static qboolean SV_IPFilterIncludesIPFilter( ipfilter_t *a, ipfilter_t *b )
{
	if( NET_NetadrType( &a->adr ) != NET_NetadrType( &b->adr ))
		return false;

	// can't include bigger subnet in small
	if( a->prefixlen < b->prefixlen )
		return false;

	if( a->prefixlen == b->prefixlen )
		return NET_CompareAdr( a->adr, b->adr );

	return NET_CompareAdrByMask( a->adr, b->adr, b->prefixlen );
}

static void SV_RemoveIPFilter( ipfilter_t *toremove, qboolean removeAll, qboolean verbose )
{
	ipfilter_t *f, **back;

	back = &ipfilter;
	while( 1 )
	{
		f = *back;
		if( !f ) return;

		if( SV_IPFilterIncludesIPFilter( toremove, f ))
		{
			if( verbose )
			{
				string filterStr;

				SV_FilterToString( filterStr, sizeof( filterStr ), false, f );

				Con_Printf( "%s removed.\n", filterStr );
			}

			*back = f->next;
			back = &f->next;

			Mem_Free( f );

			if( !removeAll )
				break;
		}
		else back = &f->next;
	}
}


/*
=============================================================================

CONNECTIONLESS RATE LIMIT

A leaky bucket per source address, so an unauthenticated query cannot be used
to point this server's replies at somebody else.

Why it is needed. The connectionless handlers are ungated: they check no
password, no `public` and no `sv_lan`, and there was no rate limit anywhere in
the engine. Measured on this engine, an A2S_RULES query answers 9 bytes with
912, which is 101x amplification, and the source address of a UDP packet is
whatever the sender writes in it. So a stranger can send small packets with
your victim's address on them and have this server deliver the flood. For
comparison the sister ports in this family measure 32x (Quake III, which does
carry a leaky bucket), 23x (Quake II, which does not) and 3x (Quake).

A firewall allowlist is the primary defence and works. This exists so that a
mistake in one ufw or nft rule is the difference between "annoying" and
"catastrophic", rather than the only thing standing between the machine and
being a usable reflector. Both, not either.

Ported from ioquake3's sv_main.c, which has carried this for years, with two
deliberate changes: time is kept as double seconds from host.realtime rather
than int milliseconds, so nothing wraps on a server left up for a month, and
addresses are compared with NET_CompareBaseAdr so IPv4 and IPv6 are handled by
the code that already knows how, rather than by a second memcmp of the union.

=============================================================================
*/
#define MAX_RATE_BUCKETS 1024   // distinct addresses tracked at once
#define MAX_RATE_HASHES  256

typedef struct leakybucket_s
{
	netadr_t	adr;
	double		lasttime;	// host.realtime at the last accounting
	int		burst;
	int		hash;
	struct leakybucket_s *prev, *next;
} leakybucket_t;

static leakybucket_t	sv_buckets[MAX_RATE_BUCKETS];
static leakybucket_t	*sv_buckethashes[MAX_RATE_HASHES];

static int SV_HashForAddress( const netadr_t *adr )
{
	const uint8_t	*b = (const uint8_t *)adr;
	int		hash = 0, i;

	// everything but the trailing port, so one host behind many source ports
	// lands in one bucket. Which is the point: the port costs an attacker
	// nothing to vary.
	for( i = 0; i < (int)( sizeof( netadr_t ) - sizeof( uint16_t )); i++ )
		hash += (int)b[i] * ( i + 119 );

	hash = hash ^ ( hash >> 10 ) ^ ( hash >> 20 );

	return hash & ( MAX_RATE_HASHES - 1 );
}

static leakybucket_t *SV_BucketForAddress( netadr_t adr, int burst, double period )
{
	leakybucket_t	*bucket;
	int		hash = SV_HashForAddress( &adr );
	int		i;

	for( bucket = sv_buckethashes[hash]; bucket; bucket = bucket->next )
	{
		if( NET_CompareBaseAdr( bucket->adr, adr ))
			return bucket;
	}

	for( i = 0; i < MAX_RATE_BUCKETS; i++ )
	{
		bucket = &sv_buckets[i];

		// reclaim a bucket that has had time to drain completely. The
		// interval < 0 case catches host.realtime going backwards, which
		// happens on a timedemo or a manual clock change and would
		// otherwise strand the bucket forever.
		if( bucket->lasttime > 0.0 )
		{
			double interval = host.realtime - bucket->lasttime;

			if( interval > ( burst * period ) || interval < 0.0 )
			{
				if( bucket->prev != NULL )
					bucket->prev->next = bucket->next;
				else sv_buckethashes[bucket->hash] = bucket->next;

				if( bucket->next != NULL )
					bucket->next->prev = bucket->prev;

				memset( bucket, 0, sizeof( *bucket ));
			}
		}

		if( NET_NetadrType( &bucket->adr ) == NA_UNDEFINED && bucket->lasttime == 0.0 )
		{
			bucket->adr = adr;
			bucket->lasttime = host.realtime;
			bucket->burst = 0;
			bucket->hash = hash;

			bucket->next = sv_buckethashes[hash];
			if( sv_buckethashes[hash] != NULL )
				sv_buckethashes[hash]->prev = bucket;
			bucket->prev = NULL;
			sv_buckethashes[hash] = bucket;

			return bucket;
		}
	}

	// every bucket is in use and none has drained. That is either a real
	// flood from many addresses or a deliberate attempt to exhaust the table,
	// and in both cases the safe answer is to say no.
	return NULL;
}

/*
====================
SV_RateLimitAddress

true means "over the limit, drop it". A NULL bucket, meaning the table is
full, also means true: fail closed, never open.
====================
*/
qboolean SV_RateLimitAddress( netadr_t adr, int burst, double period )
{
	leakybucket_t	*bucket;
	double		interval;
	int		expired;

	if( burst <= 0 || period <= 0.0 )
		return false; // disabled

	bucket = SV_BucketForAddress( adr, burst, period );
	if( bucket == NULL )
		return true;

	interval = host.realtime - bucket->lasttime;
	if( interval < 0.0 )
		interval = 0.0;

	expired = (int)( interval / period );

	if( expired > bucket->burst )
	{
		bucket->burst = 0;
		bucket->lasttime = host.realtime;
	}
	else
	{
		bucket->burst -= expired;
		bucket->lasttime = host.realtime - ( interval - ( expired * period ));
	}

	if( bucket->burst < burst )
	{
		bucket->burst++;
		return false;
	}

	return true;
}

static void SV_InitRateLimit( void )
{
	memset( sv_buckets, 0, sizeof( sv_buckets ));
	memset( sv_buckethashes, 0, sizeof( sv_buckethashes ));
}

qboolean SV_CheckIP( netadr_t *adr )
{
	for( ipfilter_t *entry = ipfilter; entry; entry = entry->next )
	{
		if( entry->endTime && host.realtime > entry->endTime )
			continue; // expired

		switch( NET_NetadrType( &entry->adr ))
		{
		case NA_IP:
		case NA_IP6:
			if( NET_CompareAdrByMask( *adr, entry->adr, entry->prefixlen ))
				return true;
			break;
		}
	}

	return false;
}

static void SV_AddIP_PrintUsage( void )
{
	Con_Printf(S_USAGE "addip <minutes> <ipaddress>\n"
		S_USAGE_INDENT "addip <minutes> <ipaddress/CIDR>\n"
		"Use 0 minutes for permanent\n"
		"ipaddress A.B.C.D/24 is equivalent to A.B.C.0 and A.B.C\n"
		"NOTE: IPv6 addresses only support prefix format!\n");
}

static void SV_RemoveIP_PrintUsage( void )
{
	Con_Printf(S_USAGE "removeip <ipaddress> [removeAll]\n"
		S_USAGE_INDENT "removeip <ipaddress/CIDR> [removeAll]\n"
		"Use removeAll to delete all ip filters which ipaddress or ipaddress/CIDR includes\n");
}

static void SV_ListIP_PrintUsage( void )
{
	Con_Printf(S_USAGE "listip [ipaddress]\n"
		S_USAGE_INDENT  "listip [ipaddress/CIDR]\n");
}

static void SV_AddIP_f( void )
{
	const char *szMinutes = Cmd_Argv( 1 );
	const char *adr = Cmd_Argv( 2 );
	ipfilter_t filter, *newfilter;
	float minutes;

	if( Cmd_Argc() != 3 )
	{
		// a1ba: kudos to rehlds for an idea of using CIDR prefixes
		// in these commands :)
		SV_AddIP_PrintUsage();
		return;
	}

	minutes = Q_atof( szMinutes );
	if( minutes < 0.1f )
		minutes = 0;

	if( minutes != 0.0f )
		filter.endTime = host.realtime + minutes * 60;
	else filter.endTime = 0;

	if( !NET_StringToFilterAdr( adr, &filter.adr, &filter.prefixlen ))
	{
		Con_Printf( "Invalid IP address!\n" );
		SV_AddIP_PrintUsage();
		return;
	}

	newfilter = Mem_Malloc( host.mempool, sizeof( *newfilter ));
	newfilter->endTime = filter.endTime;
	newfilter->adr = filter.adr;
	newfilter->prefixlen = filter.prefixlen;
	newfilter->next = ipfilter;

	ipfilter = newfilter;

	for( int i = 0; i < svs.maxclients; i++ )
	{
		netadr_t clientadr = svs.clients[i].netchan.remote_address;

		if( !NET_CompareAdrByMask( clientadr, filter.adr, filter.prefixlen ))
			continue;

		SV_ClientPrintf( &svs.clients[i], "The server operator has added you to banned list\n" );
		SV_DropClient( &svs.clients[i], false );
	}
}

static void SV_ListIP_f( void )
{
	qboolean haveFilter = false;
	ipfilter_t filter;

	if( Cmd_Argc() > 2 )
	{
		SV_ListIP_PrintUsage();
		return;
	}

	if( ipfilter == NULL )
	{
		Con_Printf( "IP filter list is empty\n" );
		return;
	}

	if( Cmd_Argc() == 2 )
	{
		haveFilter = NET_StringToFilterAdr( Cmd_Argv( 1 ), &filter.adr, &filter.prefixlen );

		if( !haveFilter )
		{
			 Con_Printf( "Invalid IP address!\n" );
			 SV_ListIP_PrintUsage();
			 return;
		}
	}

	Con_Printf( "IP filter list:\n" );

	for( ipfilter_t *f = ipfilter; f; f = f->next )
	{
		string filterStr;

		if( haveFilter && !SV_IPFilterIncludesIPFilter( &filter, f ))
			continue;

		SV_FilterToString( filterStr, sizeof( filterStr ), false, f );
		Con_Printf( "%s\n", filterStr );
	}
}

static void SV_RemoveIP_f( void )
{
	const char *adr = Cmd_Argv( 1 );
	qboolean removeAll;
	ipfilter_t filter;

	if( Cmd_Argc() != 2 && Cmd_Argc() != 3 )
	{
		SV_RemoveIP_PrintUsage();
		return;
	}

	removeAll = Cmd_Argc() == 3 && !Q_strcmp( Cmd_Argv( 2 ), "removeAll" );

	if( !NET_StringToFilterAdr( adr, &filter.adr, &filter.prefixlen ))
	{
		Con_Printf( "Invalid IP address!\n" );
		SV_RemoveIP_PrintUsage();
		return;
	}

	SV_RemoveIPFilter( &filter, removeAll, true );
}

static void SV_WriteIP_f( void )
{
	file_t *fd = FS_Open( Cvar_VariableString( "listipcfgfile" ), "w", true );

	if( !fd )
	{
		Con_Printf( "Couldn't open listip.cfg\n" );
		return;
	}

	for( ipfilter_t *f = ipfilter; f; f = f->next )
	{
		string filterStr;
		int size;

		// do not save temporary bans
		if( f->endTime )
			continue;

		size = SV_FilterToString( filterStr, sizeof( filterStr ), true, f );
		FS_Write( fd, filterStr, size );
	}

	FS_Close( fd );
}

static void SV_InitIPFilter( void )
{
	Cmd_AddRestrictedCommand( "addip", SV_AddIP_f, "add entry to IP filter" );
	Cmd_AddRestrictedCommand( "listip", SV_ListIP_f, "list current IP filter" );
	Cmd_AddRestrictedCommand( "removeip", SV_RemoveIP_f, "remove IP filter" );
	Cmd_AddRestrictedCommand( "writeip", SV_WriteIP_f, "write listip.cfg" );
}

static void SV_ShutdownIPFilter( void )
{
	// should be called manually because banned.cfg is not executed by engine
	//SV_WriteIP_f();

	for( ipfilter_t *ipList = ipfilter, *ipNext; ipList; ipList = ipNext )
	{
		ipNext = ipList->next;
		Mem_Free( ipList );
	}

	ipfilter = NULL;
}

void SV_InitFilter( void )
{
	SV_InitIPFilter();
	SV_InitIDFilter();
	SV_InitRateLimit();
}

void SV_ShutdownFilter( void )
{
	SV_ShutdownIPFilter();
	SV_ShutdownIDFilter();
}

#if XASH_ENGINE_TESTS

#include "tests.h"

static void Test_StringToFilterAdr( void )
{
	ipfilter_t f1;
	struct
	{
		const char *str;
		qboolean valid;
		int prefixlen;
		int a, b, c, d;
	} ipv4tests[] =
	{
	{ "127.0.0.0/8", true, 8, 127, 0, 0, 0 },
	{ "192.168",     true, 16, 192, 168, 0, 0 },
	{ "192.168/23",  true, 23, 192, 168, 0, 0 },
	{ "192.168./23", true, 23, 192, 168, 0, 0 },
	{ "192.168../23", true, 23, 192, 168, 0, 0 },
	{ "..192...168/23", false },
	{ "", false },
	{ "abcd", false }
	};
	struct
	{
		const char *str;
		qboolean valid;
		int prefixlen;
		uint8_t x[16];
	} ipv6tests[] =
	{
	{ "::1", true, 128, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1 } },
	{ "fd18:b9d4:65cf:83de::/64", true, 64, { 0xfd, 0x18, 0xb9, 0xd4, 0x65, 0xcf, 0x83, 0xde } },
	{ "kkljnljkhfjnkj", false },
	{ "fd8a:63d5:e014:0d62:ffff:ffff:ffff:ffff:ffff", false },
	};

	for( int i = 0; i < ARRAYSIZE( ipv4tests ); i++ )
	{
		qboolean ret = NET_StringToFilterAdr( ipv4tests[i].str, &f1.adr, &f1.prefixlen );

		TASSERT_EQi( ret, ipv4tests[i].valid );

		if( ret )
		{
			TASSERT_EQi( f1.prefixlen, ipv4tests[i].prefixlen );
			TASSERT_EQi( f1.adr.ip[0], ipv4tests[i].a );
			TASSERT_EQi( f1.adr.ip[1], ipv4tests[i].b );
			TASSERT_EQi( f1.adr.ip[2], ipv4tests[i].c );
			TASSERT_EQi( f1.adr.ip[3], ipv4tests[i].d );
		}
	}

	for( int i = 0; i < ARRAYSIZE( ipv6tests ); i++ )
	{
		qboolean ret = NET_StringToFilterAdr( ipv6tests[i].str, &f1.adr, &f1.prefixlen );
		uint8_t x[16];

		TASSERT_EQi( ret, ipv6tests[i].valid );

		if( ret )
		{
			TASSERT_EQi( f1.prefixlen, ipv6tests[i].prefixlen );

			NET_NetadrToIP6Bytes( (uint8_t*)x, &f1.adr );

			TASSERT( memcmp( x, ipv6tests[i].x, sizeof( x )) == 0 );
		}
	}
}

static void Test_IPFilterIncludesIPFilter( void )
{
	qboolean ret;
	const char *adrs[] =
	{
		"127.0.0.1/8", // 0
		"127.0.0.1", // 1
		"192.168/16", // 2
		"fe80::/64", // 3
		"fe80::96ab:9a49:2944:1808", // 4
		"2a00:1370:8190:f9eb::/62", // 5
		"2a00:1370:8190:f9eb:3866:6126:330c:b82b" // 6
	};
	ipfilter_t f[7];
	int tests[][3] =
	{
		// ipv4
		{ 0, 0, true },
		{ 0, 1, false },
		{ 1, 0, true },
		{ 0, 2, false },
		{ 2, 0, false },

		// mixed
		{ 0, 3, false },
		{ 1, 4, false },

		// ipv6
		{ 3, 3, true },
		{ 3, 4, false },
		{ 4, 3, true },
		{ 5, 3, false },
		{ 3, 5, false },
		{ 6, 5, true },
	};

	for( int i = 0; i < 7; i++ )
	{
		NET_StringToFilterAdr( adrs[i], &f[i].adr, &f[i].prefixlen );
	}

	for( int i = 0; i < ARRAYSIZE( tests ); i++ )
	{
		ret = SV_IPFilterIncludesIPFilter( &f[tests[i][0]], &f[tests[i][1]] );

		TASSERT_EQi( ret, tests[i][2] );
	}
}

void Test_RunIPFilter( void )
{
	Test_StringToFilterAdr();
	Test_IPFilterIncludesIPFilter();
}

#endif // XASH_ENGINE_TESTS
