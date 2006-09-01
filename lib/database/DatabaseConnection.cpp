// --------------------------------------------------------------------------
//
// File
//		Name:    DatabaseConnection.cpp
//		Purpose: Database connection abstraction
//		Created: 1/5/04
//
// --------------------------------------------------------------------------

#include "Box.h"

#include "DatabaseConnection.h"
#include "autogen_DatabaseException.h"
#include "DatabaseDriver.h"
#include "DatabaseDriverRegistration.h"

#include "MemLeakFindOn.h"


// --------------------------------------------------------------------------
//
// Function
//		Name:    DatabaseConnection::DatabaseConnection()
//		Purpose: Constructor
//		Created: 1/5/04
//
// --------------------------------------------------------------------------
DatabaseConnection::DatabaseConnection()
	: mpDriver(0)
{
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DatabaseConnection::~DatabaseConnection()
//		Purpose: Destructor
//		Created: 1/5/04
//
// --------------------------------------------------------------------------
DatabaseConnection::~DatabaseConnection()
{
	// Disconnect if a connection is active
	if(mpDriver != 0)
	{
		Disconnect();
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DatabaseConnection::Connect(const std::string &, const std::string &, int)
//		Purpose: Connect to a database, given a driver name and a driver, and a timeout value (in ms)
//				 connection string.
//		Created: 1/5/04
//
// --------------------------------------------------------------------------
void DatabaseConnection::Connect(const std::string &rDriverName, const std::string &rConnectionString, int Timeout)
{
	if(mpDriver != 0)
	{
		THROW_EXCEPTION(DatabaseException, AlreadyConnected)
	}

	// Create a driver object
	DatabaseDriver *pdriver = Database::CreateInstanceOfRegisteredDriver(rDriverName.c_str());

	try
	{
		// Connect
		pdriver->Connect(rConnectionString, Timeout);
	}
	catch(...)
	{
		delete pdriver;
		pdriver = 0;
		throw;
	}

	// Store connection
	mpDriver = pdriver;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DatabaseConnection::Disconnect()
//		Purpose: Disconnect from the database server
//		Created: 8/5/04
//
// --------------------------------------------------------------------------
void DatabaseConnection::Disconnect()
{
	if(mpDriver == 0)
	{
		THROW_EXCEPTION(DatabaseException, NotConnected)
	}
	
	// Ask driver to disconnect
	mpDriver->Disconnect();
	
	// Clean up
	delete mpDriver;
	mpDriver = 0;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DatabaseConnection::GetDriver()
//		Purpose: Return a reference to the driver for the connection -- internal
//				 use only.
//		Created: 8/5/04
//
// --------------------------------------------------------------------------
DatabaseDriver &DatabaseConnection::GetDriver() const
{
	if(mpDriver == 0)
	{
		THROW_EXCEPTION(DatabaseException, NotConnected)
	}

	return *mpDriver;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DatabaseConnection::GetLastAutoIncrementValue(const char *, const char *)
//		Purpose: Return the value of the auto_increment column in the last INSERT
//				 statement (use generic type `AUTO_INCREMENT_INT for column type).
//				 Specify the table and column names, as some drivers require this info.
//		Created: 15/5/04
//
// --------------------------------------------------------------------------
int32_t DatabaseConnection::GetLastAutoIncrementValue(const char *TableName, const char *ColumnName)
{
	if(mpDriver == 0)
	{
		THROW_EXCEPTION(DatabaseException, NotConnected)
	}

	// Pass on to driver	
	return mpDriver->GetLastAutoIncrementValue(TableName, ColumnName);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DatabaseConnection::QuoteString(const char *, std::string &)
//		Purpose: Quote a string in a way which is acceptable to the database.
//				 Can only be used after the database is connected.
//		Created: 11/2/05
//
// --------------------------------------------------------------------------
void DatabaseConnection::QuoteString(const char *pString, std::string &rStringQuotedOut) const
{
	if(mpDriver == 0)
	{
		THROW_EXCEPTION(DatabaseException, NotConnected)
	}
	
	// Pass on to driver	
	mpDriver->QuoteString(pString, rStringQuotedOut);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DatabaseConnection::GetDriverName()
//		Purpose: Return the name of the driver for the current connection
//		Created: 26/12/04
//
// --------------------------------------------------------------------------
const char *DatabaseConnection::GetDriverName() const
{
	if(mpDriver == 0)
	{
		THROW_EXCEPTION(DatabaseException, NotConnected)
	}

	// Pass on to driver	
	return mpDriver->GetDriverName();
}


