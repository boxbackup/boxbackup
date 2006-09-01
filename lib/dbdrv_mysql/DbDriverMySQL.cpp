// --------------------------------------------------------------------------
//
// File
//		Name:    DbDriverMySQL.cpp
//		Purpose: Database driver for MySQL
//		Created: 11/5/04
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <stdlib.h>
#include <string.h>
#include <memory>

#include "DbDriverMySQL.h"
#include "DbQueryMySQL.h"
#include "autogen_DatabaseException.h"
#include "Utils.h"

#include "MemLeakFindOn.h"


// --------------------------------------------------------------------------
//
// Function
//		Name:    DbDriverMySQL::DbDriverMySQL()
//		Purpose: Constructor
//		Created: 11/5/04
//
// --------------------------------------------------------------------------
DbDriverMySQL::DbDriverMySQL()
	: mpConnection(0)
{
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DbDriverMySQL::~DbDriverMySQL()
//		Purpose: Destructor
//		Created: 11/5/04
//
// --------------------------------------------------------------------------
DbDriverMySQL::~DbDriverMySQL()
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
//		Name:    DbDriverMySQL::GetDriverName()
//		Purpose: Name of driver
//		Created: 11/5/04
//
// --------------------------------------------------------------------------
const char *DbDriverMySQL::GetDriverName() const
{
	return "mysql";
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DbDriverMySQL::Connect(const std::string &, int)
//		Purpose: Connect to database, Connection string is database:username:password, or
//				 hostname:database:username:password
//		Created: 11/5/04
//
// --------------------------------------------------------------------------
void DbDriverMySQL::Connect(const std::string &rConnectionString, int Timeout)
{
	if(mpConnection != 0)
	{
		THROW_EXCEPTION(DatabaseException, AlreadyConnected)
	}
	
	// Decode the connection string
	std::vector<std::string> elements;
	SplitString(rConnectionString, ':', elements);
	const char *hostname = 0;
	int ps = 0;
	switch(elements.size())
	{
	case 3:
		ps = 0;
		break;
	case 4:
		hostname = elements[0].c_str();
		ps = 1;
		break;
	default:
		THROW_EXCEPTION(DatabaseException, BadConnectionString)
		break;
	}
	const char *database = elements[ps+0].c_str();
	const char *username = elements[ps+1].c_str();
	const char *password = elements[ps+2].c_str();
	if(hostname != 0 && hostname[0] == '\0') hostname = NULL;
	if(database[0] == '\0') database = NULL;
	if(username[0] == '\0') username = NULL;
	if(password[0] == '\0') password = NULL;
	

	// Allocate a connection object
	MYSQL *pconnection = ::mysql_init(NULL);
	if(pconnection == NULL)
	{
		THROW_EXCEPTION(DatabaseException, FailedToConnect)
	}

	try
	{
		// Set connect timeout
		unsigned int timeout = (Timeout + 999) / 1000;	// in seconds, rounded up
		::mysql_options(pconnection, MYSQL_OPT_CONNECT_TIMEOUT, (const char *)(&timeout));
		
		// Connect!
		if(::mysql_real_connect(pconnection, hostname, username, password, database, 0, NULL, 0) == NULL)
		{
			ReportMySQLError(pconnection);
			THROW_EXCEPTION(DatabaseException, FailedToConnect)
		}
	}
	catch(...)
	{
		::mysql_close(pconnection);
		throw;
	}
	
	// Store connection
	mpConnection = pconnection;		
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DbDriverMySQL::ReportMySQLError(MYSQL *)
//		Purpose: Report the error from the mysql interface. If arg is zero, then use current connection.
//		Created: 11/5/04
//
// --------------------------------------------------------------------------
void DbDriverMySQL::ReportMySQLError(MYSQL *pConnection)
{
	MYSQL *pconn = pConnection;
	if(pconn == NULL)
	{
		pconn = mpConnection;
	}

	if(pconn != 0)
	{
		ReportErrorMessage(::mysql_error(pconn));
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DbDriverMySQL::Query()
//		Purpose: Returns a new query object
//		Created: 11/5/04
//
// --------------------------------------------------------------------------
DatabaseDrvQuery *DbDriverMySQL::Query()
{
	return new DbQueryMySQL(*this);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DbDriverMySQL::Disconnect()
//		Purpose: Disconnect from the database
//		Created: 11/5/04
//
// --------------------------------------------------------------------------
void DbDriverMySQL::Disconnect()
{
	if(mpConnection == 0)
	{
		// Nothing to do
		return;
	}

	// Close connection to database
	::mysql_close(mpConnection);
	mpConnection = 0;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DbDriverMySQL::QuoteString(const char *, std::string &)
//		Purpose: Quote a string ready for inclusion into some nice SQL
//		Created: 11/5/04
//
// --------------------------------------------------------------------------
void DbDriverMySQL::QuoteString(const char *pString, std::string &rStringQuotedOut) const
{
	ASSERT(pString != 0);

	// Not an amazingly efficient interface, but there you go
	unsigned long stringLength = ::strlen(pString);
	unsigned long bufferSize = (stringLength * 2) + 6;	// calcuation from MySQL docs, with extra space for quotes
	char *buffer = (char *)::malloc(bufferSize);
	if(buffer == NULL)
	{
		throw std::bad_alloc();
	}
	
	// Initial quote char (MySQL doesn't do the quote chars for us)
	buffer[0] = '\'';
	
	// Quote string
	unsigned long len = ::mysql_real_escape_string(mpConnection, buffer + 1, pString, stringLength);

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
//		Name:    DbDriverMySQL::GetLastAutoIncrementValue(const char *, const char *)
//		Purpose: Get the last inserted value
//		Created: 15/5/04
//
// --------------------------------------------------------------------------
int32_t DbDriverMySQL::GetLastAutoIncrementValue(const char *TableName, const char *ColumnName)
{
	my_ulonglong id = ::mysql_insert_id(mpConnection);
	return (int32_t)id;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DbDriverMySQL::GetGenericTranslations()
//		Purpose: Get translations for generics in SQL statements
//		Created: 15/5/04
//
// --------------------------------------------------------------------------
const DatabaseDriver::TranslateMap_t &DbDriverMySQL::GetGenericTranslations()
{
	static DatabaseDriver::TranslateMap_t table;
	const char *from[] = {"AUTO_INCREMENT_INT", "LIMIT2", "CREATE_INDEX_CASE_INSENSTIVE3", "COLUMN_CASE_INSENSITIVE_ORDERING", "CASE_INSENSITIVE_COLUMN1", 0};
	const char *to[] = {"INT NOT NULL PRIMARY KEY AUTO_INCREMENT", "LIMIT !0,!1", "CREATE INDEX !0 ON !1 (!2)", "", "!0", 0};
	DATABASE_DRIVER_FILL_TRANSLATION_TABLE(table, from, to);
	return table;
}


