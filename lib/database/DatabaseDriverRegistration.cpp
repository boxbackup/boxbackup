// --------------------------------------------------------------------------
//
// File
//		Name:    DatabaseDriverRegistration.cpp
//		Purpose: Create a new driver -- contains a list of all drivers
//		Created: 2/5/04
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <string.h>

#include "DatabaseDriverRegistration.h"
#include "autogen_DatabaseException.h"

// Find out which driver modules are available
#include "../../local/modules.h"

// load module headers
#ifdef MODULE_lib_dbdrv_sqlite
	#include "DbDriverSqlite.h"
#endif
#ifdef MODULE_lib_dbdrv_mysql
	#include "DbDriverMySQL.h"
#endif
#ifdef MODULE_lib_dbdrv_postgresql
	#include "DbDriverPostgreSQL.h"
#endif

#include "MemLeakFindOn.h"


// --------------------------------------------------------------------------
//
// Function
//		Name:    Database::CreateInstanceOfRegisteredDriver(const char *)
//		Purpose: Returns an initialised (but unconnected) driver object for a
//				 given driver name. Will exception if the driver is unavailable.
//		Created: 2/5/04
//
// --------------------------------------------------------------------------
DatabaseDriver *Database::CreateInstanceOfRegisteredDriver(const char *DriverName)
{
#ifdef MODULE_lib_dbdrv_sqlite
	if(::strcmp(DriverName, "sqlite") == 0)
	{
		return new DbDriverSqlite;
	}
#endif
#ifdef MODULE_lib_dbdrv_mysql
	if(::strcmp(DriverName, "mysql") == 0)
	{
		return new DbDriverMySQL;
	}
#endif
#ifdef MODULE_lib_dbdrv_postgresql
	if(::strcmp(DriverName, "postgresql") == 0)
	{
		return new DbDriverPostgreSQL;
	}
#endif

	THROW_EXCEPTION(DatabaseException, DriverNotAvailable)
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    Database::DriverAvailable(const char *)
//		Purpose: Returns true if a particular driver is available
//		Created: 10/5/04
//
// --------------------------------------------------------------------------
bool Database::DriverAvailable(const char *DriverName)
{
#ifdef MODULE_lib_dbdrv_sqlite
	if(::strcmp(DriverName, "sqlite") == 0)
	{
		return true;
	}
#endif
#ifdef MODULE_lib_dbdrv_mysql
	if(::strcmp(DriverName, "mysql") == 0)
	{
		return true;
	}
#endif
#ifdef MODULE_lib_dbdrv_postgresql
	if(::strcmp(DriverName, "postgresql") == 0)
	{
		return true;
	}
#endif

	return false;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    Database::DriverList(std::string &)
//		Purpose: Returns the number of drivers available, and fills in a string
//				 with a list of those drivers.
//		Created: 10/5/04
//
// --------------------------------------------------------------------------
int Database::DriverList(const char **pDriverList)
{
	if(pDriverList)
	{
		*pDriverList =
#ifdef MODULE_lib_dbdrv_sqlite
		 "sqlite "\

#endif
#ifdef MODULE_lib_dbdrv_mysql
		 "mysql "\

#endif
#ifdef MODULE_lib_dbdrv_postgresql
		 "postgresql "\

#endif
			"";
	}

	int nDrivers = 0;
#ifdef MODULE_lib_dbdrv_sqlite
	++nDrivers;
#endif
#ifdef MODULE_lib_dbdrv_mysql
	++nDrivers;
#endif
#ifdef MODULE_lib_dbdrv_postgresql
	++nDrivers;
#endif

	return nDrivers;
}

