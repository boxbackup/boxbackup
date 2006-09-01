// --------------------------------------------------------------------------
//
// File
//		Name:    TestWebApp.cpp
//		Purpose: Test web application
//		Created: 30/3/04
//
// --------------------------------------------------------------------------

#include "Box.h"

#include "TestWebApp.h"
#include "autogen_webapp/TestWebAppPageLogin.h"
#include "Configuration.h"

#include "MemLeakFindOn.h"


// --------------------------------------------------------------------------
//
// Function
//		Name:    TestWebApp::TestWebApp()
//		Purpose: Constructor
//		Created: 30/3/04
//
// --------------------------------------------------------------------------
TestWebApp::TestWebApp()
{
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    TestWebApp::~TestWebApp()
//		Purpose: Desctructor
//		Created: 30/3/04
//
// --------------------------------------------------------------------------
TestWebApp::~TestWebApp()
{
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    TestWebApp::ChildStart(const Configuration &)
//		Purpose: Called when a child is started
//		Created: 17/5/04
//
// --------------------------------------------------------------------------
void TestWebApp::ChildStart(const Configuration &rConfiguration)
{
	mDatabase.Connect(rConfiguration.GetKeyValue("DatabaseDriver"),
		rConfiguration.GetKeyValue("DatabaseConnection"), 2000 /* timeout */);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    TestWebApp::ChildFinish()
//		Purpose: Called when the child process ends
//		Created: 17/5/04
//
// --------------------------------------------------------------------------
void TestWebApp::ChildFinish()
{
	mDatabase.Disconnect();
}


