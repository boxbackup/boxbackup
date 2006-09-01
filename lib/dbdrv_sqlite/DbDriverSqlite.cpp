// --------------------------------------------------------------------------
//
// File
//		Name:    DbDriverSqlite.cpp
//		Purpose: Database driver for Sqllite
//		Created: 10/5/04
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <memory>

#include "DbDriverSqlite.h"
#include "DbQuerySqlite.h"
#include "autogen_DatabaseException.h"
#include "DbDriverSqliteV3.h"

#include "MemLeakFindOn.h"


// --------------------------------------------------------------------------
//
// Function
//		Name:    DbDriverSqlite::DbDriverSqlite()
//		Purpose: Constructor
//		Created: 10/5/04
//
// --------------------------------------------------------------------------
DbDriverSqlite::DbDriverSqlite()
	: mpConnection(0)
{
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DbDriverSqlite::~DbDriverSqlite()
//		Purpose: Destructor
//		Created: 10/5/04
//
// --------------------------------------------------------------------------
DbDriverSqlite::~DbDriverSqlite()
{
	if(mpConnection != 0)
	{
		Disconnect();
	}
	ASSERT(mpConnection == 0);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DbDriverSqlite::GetDriverName()
//		Purpose: Name of driver
//		Created: 10/5/04
//
// --------------------------------------------------------------------------
const char *DbDriverSqlite::GetDriverName() const
{
	return "sqlite";
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DbDriverSqlite::Connect(const std::string &, int)
//		Purpose: Connect to database, Connection string is filename
//		Created: 10/5/04
//
// --------------------------------------------------------------------------
void DbDriverSqlite::Connect(const std::string &rConnectionString, int Timeout)
{
	if(mpConnection != 0)
	{
		THROW_EXCEPTION(DatabaseException, AlreadyConnected)
	}

#ifdef PLATFORM_SQLITE3
	mpConnection = 0;
	if(::sqlite3_open(rConnectionString.c_str(), &mpConnection) != SQLITE_OK)
	{
		ReportErrorMessage(::sqlite3_errmsg(mpConnection));
		if(mpConnection != 0)
		{
			::sqlite3_close(mpConnection);
		}
		THROW_EXCEPTION(DatabaseException, FailedToConnect)
	}
#else
	// mode in sqlite_open is ignored at the moment, and no "future proof" value
	// seems to be defined in the documentation. So just use 0, and hope for the best.
	char *errmsg = 0;
	mpConnection = ::sqlite_open(rConnectionString.c_str(), 0 /* open mode */, &errmsg);
	if(mpConnection == NULL)
	{
		ReportErrorMessage(errmsg);
		THROW_EXCEPTION(DatabaseException, FailedToConnect)
	}
#endif
	
	// Set the timeout for waiting for the lock.
	// This is actually slightly different behaviour than a timeout for connecting to the database,
	// but is more appropraite to the database itself since there is no real notion of connecting,
	// and this will give sensible behaviour when the database gets concurrent use.
	::sqlite_busy_timeout(mpConnection, Timeout);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DbDriverSqlite::Query()
//		Purpose: Returns a new query object
//		Created: 10/5/04
//
// --------------------------------------------------------------------------
DatabaseDrvQuery *DbDriverSqlite::Query()
{
	return new DbQuerySqlite(*this);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DbDriverSqlite::Disconnect()
//		Purpose: Disconnect from the database
//		Created: 10/5/04
//
// --------------------------------------------------------------------------
void DbDriverSqlite::Disconnect()
{
	if(mpConnection == 0)
	{
		// Nothing to do
		return;
	}

	// Close connection to database
	::sqlite_close(mpConnection);
	mpConnection = 0;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DbDriverSqlite::QuoteString(const char *, std::string &)
//		Purpose: Quote a string ready for inclusion into some nice SQL
//		Created: 10/5/04
//
// --------------------------------------------------------------------------
void DbDriverSqlite::QuoteString(const char *pString, std::string &rStringQuotedOut) const
{
	ASSERT(pString != 0);

	// Ask SQLite to do this for us, in a not terribly efficient manner.
	char *quoted = ::sqlite_mprintf("%Q", pString);

	// Check returned value
	if(quoted == NULL)
	{
		throw std::bad_alloc();
	}

	// Copy into output string
	try
	{
		rStringQuotedOut = quoted;
	}
	catch(...)
	{
		::sqlite_freemem(quoted);
		throw;
	}

	// Free temporary output block
	::sqlite_freemem(quoted);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DbDriverSqlite::GetLastAutoIncrementValue(const char *, const char *)
//		Purpose: Get the last inserted value
//		Created: 15/5/04
//
// --------------------------------------------------------------------------
int32_t DbDriverSqlite::GetLastAutoIncrementValue(const char *TableName, const char *ColumnName)
{
	return ::sqlite_last_insert_rowid(mpConnection);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DbDriverSqlite::GetGenericTranslations()
//		Purpose: Get translations for generics in SQL statements
//		Created: 15/5/04
//
// --------------------------------------------------------------------------
const DatabaseDriver::TranslateMap_t &DbDriverSqlite::GetGenericTranslations()
{
	static DatabaseDriver::TranslateMap_t table;
	const char *from[] = {"AUTO_INCREMENT_INT", "LIMIT2", "CREATE_INDEX_CASE_INSENSTIVE3", "COLUMN_CASE_INSENSITIVE_ORDERING", "CASE_INSENSITIVE_COLUMN1", 0};
	const char *to[] = {"INTEGER PRIMARY KEY", "LIMIT !1 OFFSET !0", "CREATE INDEX !0 ON !1 (!2)", "COLLATE NOCASE", "!0", 0};
	DATABASE_DRIVER_FILL_TRANSLATION_TABLE(table, from, to);
	return table;
}


