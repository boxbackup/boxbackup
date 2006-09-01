// --------------------------------------------------------------------------
//
// File
//		Name:    DbDriverPostgreSQL.cpp
//		Purpose: Database driver for PostgreSQL
//		Created: 26/12/04
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <memory>

#include "DbDriverPostgreSQL.h"
#include "DbQueryPostgreSQL.h"
#include "autogen_DatabaseException.h"
#include "Utils.h"

#include "MemLeakFindOn.h"


// --------------------------------------------------------------------------
//
// Function
//		Name:    static void boxPgNoticeProcessor(void *, const char *)
//		Purpose: Send PostgreSQL notices to TRACE
//		Created: 26/12/04
//
// --------------------------------------------------------------------------
static void boxPgNoticeProcessor(void *arg, const char *message)
{
    TRACE1("PG: %s", message);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DbDriverPostgreSQL::DbDriverPostgreSQL()
//		Purpose: Constructor
//		Created: 26/12/04
//
// --------------------------------------------------------------------------
DbDriverPostgreSQL::DbDriverPostgreSQL()
	: mpConnection(0)
{
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DbDriverPostgreSQL::~DbDriverPostgreSQL()
//		Purpose: Destructor
//		Created: 26/12/04
//
// --------------------------------------------------------------------------
DbDriverPostgreSQL::~DbDriverPostgreSQL()
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
//		Name:    DbDriverPostgreSQL::GetDriverName()
//		Purpose: Name of driver
//		Created: 26/12/04
//
// --------------------------------------------------------------------------
const char *DbDriverPostgreSQL::GetDriverName() const
{
	return "postgresql";
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DbDriverPostgreSQL::Connect(const std::string &, int)
//		Purpose: Connect to database, Connection string is as defined by libpq library
//		Created: 26/12/04
//
// --------------------------------------------------------------------------
void DbDriverPostgreSQL::Connect(const std::string &rConnectionString, int Timeout)
{
	if(mpConnection != 0)
	{
		THROW_EXCEPTION(DatabaseException, AlreadyConnected)
	}

	// Generate the connection info string
	std::string conninfo(rConnectionString);
	// If there isn't a timeout specified in the string, add it
	if(conninfo.find("connect_timeout") == std::string::npos)
	{
		char timeout[64];
		::sprintf(timeout, " connect_timeout = %d", (Timeout + 999) / 1000);	// in seconds, rounded up
		conninfo += timeout;
	}
	TRACE1("DbDriverPostgesSQL, using conninfo = '%s'\n", conninfo.c_str());

	// Allocate a connection object
	PGconn *pconnection = ::PQconnectdb(rConnectionString.c_str());
	if(pconnection == NULL)
	{
		THROW_EXCEPTION(DatabaseException, FailedToConnect)
	}

	// Set nofity processor to send notices to TRACE
	::PQsetNoticeProcessor(pconnection, boxPgNoticeProcessor, 0);

	// Check connection worked OK
	if(::PQstatus(pconnection) != CONNECTION_OK)
	{
		ReportPostgreSQLError(pconnection);
		::PQfinish(pconnection);
		THROW_EXCEPTION(DatabaseException, FailedToConnect)
	}

	// Store connection
	mpConnection = pconnection;		
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DbDriverPostgreSQL::ReportPostgreSQLError(PGconn *)
//		Purpose: Report the error from the mysql interface. If arg is zero, then use current connection.
//		Created: 26/12/04
//
// --------------------------------------------------------------------------
void DbDriverPostgreSQL::ReportPostgreSQLError(PGconn *pConnection)
{
	PGconn *pconn = pConnection;
	if(pconn == NULL)
	{
		pconn = mpConnection;
	}

	if(pconn != 0)
	{
		ReportErrorMessage(::PQerrorMessage(pconn));
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DbDriverPostgreSQL::Query()
//		Purpose: Returns a new query object
//		Created: 26/12/04
//
// --------------------------------------------------------------------------
DatabaseDrvQuery *DbDriverPostgreSQL::Query()
{
	return new DbQueryPostgreSQL(*this);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DbDriverPostgreSQL::Disconnect()
//		Purpose: Disconnect from the database
//		Created: 26/12/04
//
// --------------------------------------------------------------------------
void DbDriverPostgreSQL::Disconnect()
{
	if(mpConnection == 0)
	{
		// Nothing to do
		return;
	}

	// Close connection to database
	::PQfinish(mpConnection);
	mpConnection = 0;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DbDriverPostgreSQL::QuoteString(const char *, std::string &)
//		Purpose: Quote a string ready for inclusion into some nice SQL
//		Created: 26/12/04
//
// --------------------------------------------------------------------------
void DbDriverPostgreSQL::QuoteString(const char *pString, std::string &rStringQuotedOut) const
{
	ASSERT(pString != 0);

	// Not an amazingly efficient interface, but there you go
	unsigned long stringLength = ::strlen(pString);
	unsigned long bufferSize = (stringLength * 2) + 6;	// calcuation from PostgreSQL docs, with extra space for quotes
	char *buffer = (char *)::malloc(bufferSize);
	if(buffer == NULL)
	{
		throw std::bad_alloc();
	}
	
	// Initial quote char (PostgreSQL doesn't do the quote chars for us)
	buffer[0] = '\'';
	
	// Quote string
	size_t len = ::PQescapeString(buffer + 1, pString, stringLength);

	// Add final quote char and terminator
	buffer[len + 1] = '\'';
	buffer[len + 2] = '\0';

	// Copy into output string
	try
	{
		rStringQuotedOut.assign(buffer, len + 2);
	}
	catch(...)
	{
		::free(buffer);
		throw;
	}

	// Free temporary output block
	::free(buffer);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DbDriverPostgreSQL::GetLastAutoIncrementValue(const char *, const char *)
//		Purpose: Get the last inserted value
//		Created: 26/12/04
//
// --------------------------------------------------------------------------
int32_t DbDriverPostgreSQL::GetLastAutoIncrementValue(const char *TableName, const char *ColumnName)
{
	// Generate query to find the value by querying the appropraite sequence
	std::string sql("SELECT currval('");
	sql += TableName;
	sql += '_';
	sql += ColumnName;
	sql += "_seq')";

	// Run query
	DbQueryPostgreSQL query(*this);
	query.Execute(sql.c_str(), 0, NULL, NULL);
	if(!query.Next())
	{
		THROW_EXCEPTION(DatabaseException, FailedToRetrieveAutoIncValue)
	}
	else
	{
		return query.GetFieldInt(0);
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DbDriverPostgreSQL::GetGenericTranslations()
//		Purpose: Get translations for generics in SQL statements
//		Created: 26/12/04
//
// --------------------------------------------------------------------------
const DatabaseDriver::TranslateMap_t &DbDriverPostgreSQL::GetGenericTranslations()
{
	static DatabaseDriver::TranslateMap_t table;
	const char *from[] = {"AUTO_INCREMENT_INT", "LIMIT2", "CREATE_INDEX_CASE_INSENSTIVE3", "COLUMN_CASE_INSENSITIVE_ORDERING", "CASE_INSENSITIVE_COLUMN1", 0};
	const char *to[] = {"SERIAL PRIMARY KEY", "LIMIT !1 OFFSET !0", "CREATE INDEX !0 ON !1 (LOWER(!2))", "", "LOWER(!0)", 0};
	DATABASE_DRIVER_FILL_TRANSLATION_TABLE(table, from, to);
	return table;
}


