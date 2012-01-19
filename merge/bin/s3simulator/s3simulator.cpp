// --------------------------------------------------------------------------
//
// File
//		Name:    s3simulator.cpp
//		Purpose: main file for S3 simulator daemon
//		Created: 2003/10/11
//
// --------------------------------------------------------------------------

#include "Box.h"
#include "S3Simulator.h"
#include "MainHelper.h"

#include "MemLeakFindOn.h"

int main(int argc, const char *argv[])
{
	int ExitCode = 0;

	MAINHELPER_START

	Logging::SetProgramName("s3simulator");
	Logging::ToConsole(true);
	Logging::ToSyslog (true);
	
	S3Simulator daemon;
	ExitCode = daemon.Main("s3simulator.conf", argc, argv);

	MAINHELPER_END

	return ExitCode;
}
