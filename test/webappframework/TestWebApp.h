// --------------------------------------------------------------------------
//
// File
//		Name:    TestWebApp.h
//		Purpose: Test web application
//		Created: 30/3/04
//
// --------------------------------------------------------------------------

#ifndef TESTWEBAPP__H
#define TESTWEBAPP__H

#include "WebApplicationObject.h"
#include "DatabaseConnection.h"

// --------------------------------------------------------------------------
//
// Class
//		Name:    TestWebApp
//		Purpose: Test web application
//		Created: 30/3/04
//
// --------------------------------------------------------------------------
class TestWebApp : public WebApplicationObject
{
public:
	TestWebApp();
	~TestWebApp();

	void ChildStart(const Configuration &rConfiguration);
	void ChildFinish();

	DatabaseConnection &GetDatabaseConnection() {return mDatabase;}

private:
	DatabaseConnection mDatabase;
};

#endif // TESTWEBAPP__H

