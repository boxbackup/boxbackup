//
//	AUTOMATICALLY GENERATED FILE
//		do not edit
//


// --------------------------------------------------------------------------
//
// File
//		Name:    testmain.template.h
//		Purpose: Template file for running tests
//		Created: 2003/07/08
//
// --------------------------------------------------------------------------

#include "Box.h"

#include "stdio.h"
#include <exception>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <syslog.h>
#include <stdarg.h>
#include <fcntl.h>
#include <errno.h>

#include "MemLeakFindOn.h"

int test(int argc, const char *argv[]);

#ifdef NDEBUG
	#define MODE_TEXT	"release"
#else
	#define MODE_TEXT	"debug"
#endif

int failures = 0;

int filedes_open_at_beginning = -1;

int count_filedes()
{
	int c = 0;

	// See how many file descriptors there are with values < 256
	for(int d = 0; d < 256; ++d)
	{
		if(::fcntl(d, F_GETFD) != -1)
		{
			// File descriptor obviously exists
			++c;
		}
	}
	
	return c;
}

bool checkfilesleftopen()
{
	if(filedes_open_at_beginning == -1)
	{
		// Not used correctly, pretend that there were things left open so this gets invesitgated
		return true;
	}

	// make sure syslog log file is closed, if it was opened
	::closelog();

	// Count the file descriptors open
	return filedes_open_at_beginning != count_filedes();
}

int main(int argc, const char *argv[])
{
	// Start memory leak testing
	MEMLEAKFINDER_START

	// If there is more than one argument, then the test is doing something advanced, so leave it alone
	bool fulltestmode = (argc == 1);

	if(fulltestmode)
	{
		// Count open file descriptors for a very crude "files left open" test
		filedes_open_at_beginning = count_filedes();

		// banner
		printf("Running test TEST_NAME in " MODE_TEXT " mode...\n");
	}
	try
	{
		int returncode = test(argc, argv);
		
		// check for memory leaks, if enabled
		#ifdef BOX_MEMORY_LEAK_TESTING
			if(memleakfinder_numleaks() != 0)
			{
				failures++;
				printf("FAILURE: Memory leaks detected\n");
				printf("==== MEMORY LEAKS =================================\n");
				memleakfinder_reportleaks();
				printf("===================================================\n");
			}
		#endif
		
		if(fulltestmode)
		{
			bool filesleftopen = checkfilesleftopen();
			if(filesleftopen)
			{
				failures++;
				printf("IMPLICIT TEST FAILED: Something left files open\n");
			}
			if(failures > 0)
			{
				printf("FAILED: %d tests failed\n", failures);
			}
			else
			{
				printf("PASSED\n");
			}
		}
		
		return returncode;
	}
	catch(std::exception &e)
	{
		printf("FAILED: Exception caught: %s\n", e.what());
		return 1;
	}
	catch(...)
	{
		printf("FAILED: Unknown exception caught\n");
		return 1;
	}
	if(fulltestmode)
	{
		if(checkfilesleftopen())
		{
			printf("WARNING: Files were left open\n");
		}
	}
}

