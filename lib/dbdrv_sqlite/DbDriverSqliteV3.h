// --------------------------------------------------------------------------
//
// File
//		Name:    DbDriverSqliteV3.h
//		Purpose: Compatibility layer for v3 sqlite
//		Created: 4/11/04
//
// --------------------------------------------------------------------------

#ifndef DBDRIVERSQLITEV3__H
#define DBDRIVERSQLITEV3__H

// The sqlite driver was originally written for the v2 series of SQLite.
// Redefine things to use the new names. Incompatible functions are done with
// ifdefs in the usual way.

#ifdef PLATFORM_SQLITE3
	#define sqlite_busy_timeout			sqlite3_busy_timeout
	#define	sqlite_close				sqlite3_close
	#define sqlite_freemem				sqlite3_free
	#define sqlite_mprintf				sqlite3_mprintf
	#define sqlite_last_insert_rowid	sqlite3_last_insert_rowid
	#define sqlite_get_table			sqlite3_get_table
	#define sqlite_changes				sqlite3_changes
	#define sqlite_free_table			sqlite3_free_table
#endif

#endif // DBDRIVERSQLITEV3__H

