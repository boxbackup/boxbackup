// --------------------------------------------------------------------------
//
// File
//		Name:    HTTPTest.cpp
//		Purpose: Amazon S3 simulator start/stop functions
//		Created: 14/11/2016
//
// --------------------------------------------------------------------------

#include "Box.h"

#include "ServerControl.h"
#include "Utils.h"

bool kill_simulator_if_running()
{
	bool success = true;

	if(FileExists("testfiles/s3simulator.pid"))
	{
		TEST_THAT_OR(KillServer("testfiles/s3simulator.pid", true), success = false);
	}

	return success;
}

