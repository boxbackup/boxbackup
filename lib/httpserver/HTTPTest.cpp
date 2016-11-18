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

static int s3simulator_pid;

bool StartSimulator()
{
	s3simulator_pid = StartDaemon(s3simulator_pid,
		"../../bin/s3simulator/s3simulator " + bbstored_args +
		" testfiles/s3simulator.conf", "testfiles/s3simulator.pid");
	return s3simulator_pid != 0;
}

bool StopSimulator()
{
	bool result = StopDaemon(s3simulator_pid, "testfiles/s3simulator.pid",
		"s3simulator.memleaks", true);
	s3simulator_pid = 0;
	return result;
}

bool kill_simulator_if_running()
{
	bool success = true;

	if(FileExists("testfiles/s3simulator.pid"))
	{
		TEST_THAT_OR(KillServer("testfiles/s3simulator.pid", true), success = false);
	}

	return success;
}

