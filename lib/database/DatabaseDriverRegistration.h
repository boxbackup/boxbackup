// --------------------------------------------------------------------------
//
// File
//		Name:    DatabaseDriverRegistration.h
//		Purpose: Database driver registration
//		Created: 10/5/04
//
// --------------------------------------------------------------------------

#ifndef DATABASEDRIVERREGISTRATION__H
#define DATABASEDRIVERREGISTRATION__H

class DatabaseDriver;

namespace Database
{
	DatabaseDriver *CreateInstanceOfRegisteredDriver(const char *DriverName);
	bool DriverAvailable(const char *DriverName);
	int DriverList(const char **pDriverList = 0);
};

#endif // DATABASEDRIVERREGISTRATION__H

