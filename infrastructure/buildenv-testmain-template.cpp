//
//	AUTOMATICALLY GENERATED FILE
//		do not edit
//
//	Note that infrastructure/buildenv-testmain-template.cpp is NOT
//	auto-generated, but test/*/_main.cpp are generated from it.
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
#include <stdarg.h>
#include <fcntl.h>
#include <errno.h>
#include <string>

#ifdef HAVE_GETOPT_H
	#include <getopt.h>
#endif

#ifdef WIN32
	#include "emu.h"
#else
	#include <syslog.h>
#endif

#include "Logging.h"
#include "Test.h"
#include "Timer.h"

#include "MemLeakFindOn.h"

int test(int argc, const char *argv[]);

#ifdef NDEBUG
	#define MODE_TEXT	"release"
#else
	#define MODE_TEXT	"debug"
#endif

int failures = 0;
int first_fail_line;
std::string first_fail_file;
std::string bbackupd_args, bbstored_args, bbackupquery_args;

int filedes_open_at_beginning = -1;

#ifdef WIN32

// any way to check for open file descriptors on Win32?
inline int  count_filedes()      { return 0;     }
inline bool checkfilesleftopen() { return false; }

#else // !WIN32

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
		// Not used correctly, pretend that there were things 
		// left open so this gets investigated
		return true;
	}

	// make sure syslog log file is closed, if it was opened
	::closelog();

	// Count the file descriptors open
	return filedes_open_at_beginning != count_filedes();
}

#endif

int main(int argc, char * const * argv)
{
	// Start memory leak testing
	MEMLEAKFINDER_START

#ifdef HAVE_GETOPT_H
	struct option longopts[] = 
	{
		{ "bbackupd-args",	required_argument, NULL, 'c' },
		{ "bbstored-args",	required_argument, NULL, 's' },
		{ NULL,			0,                 NULL,  0  }
	};
	
	int ch;
	
	while ((ch = getopt_long(argc, argv, "c:s:t:T", longopts, NULL))
		!= -1)
	{
		switch(ch)
		{
			case 'c':
			{
				bbackupd_args += " ";
				bbackupd_args += optarg;
			}
			break;

			case 's':
			{
				bbstored_args += " ";
				bbstored_args += optarg;
			}
			break;

			case 't':
			{
				Console::SetTag(optarg);
			}
			break;

			case 'T':
			{
				Console::SetShowTime(true);
			}
			break;

			case '?':
			{
				fprintf(stderr, "Unknown option: %s\n",
					optarg);
				exit(2);
			}

			default:
			{
				fprintf(stderr, "Unknown option code '%c'\n",
					ch);
				exit(2);
			}
		}
	}

	argc -= optind - 1;
	argv += optind - 1;
#endif // HAVE_GETOPT_H

	// If there is more than one argument, then the test is doing something advanced, so leave it alone
	bool fulltestmode = (argc == 1);

	if(fulltestmode)
	{
		// Count open file descriptors for a very crude "files left open" test
		filedes_open_at_beginning = count_filedes();

		// banner
		printf("Running test TEST_NAME in " MODE_TEXT " mode...\n");

		#ifdef WIN32
			// Under win32 we must initialise the Winsock library
			// before using sockets

			WSADATA info;
			TEST_THAT(WSAStartup(0x0101, &info) != SOCKET_ERROR)
		#endif
	}

	try
	{
		#ifdef BOX_MEMORY_LEAK_TESTING
		memleakfinder_init();
		#endif

		Timers::Init();
		int returncode = test(argc, (const char **)argv);
		Timers::Cleanup();
		
		// check for memory leaks, if enabled
		#ifdef BOX_MEMORY_LEAK_TESTING
			if(memleakfinder_numleaks() != 0)
			{
				failures++;
				printf("FAILURE: Memory leaks detected in test code\n");
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
				printf("FAILED: %d tests failed (first at "
					"%s:%d)\n", failures, 
					first_fail_file.c_str(),
					first_fail_line);
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

