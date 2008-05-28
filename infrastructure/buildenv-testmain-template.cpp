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

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#ifdef HAVE_GETOPT_H
	#include <getopt.h>
#endif

#include <sys/stat.h>
#include <sys/types.h>

#include <exception>
#include <string>

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

#ifdef WIN32
	#define QUIET_PROCESS "-K"
#else
	#define QUIET_PROCESS ""
#endif

std::string bbackupd_args = QUIET_PROCESS,
	bbstored_args = QUIET_PROCESS,
	bbackupquery_args,
	test_args;

int filedes_open_at_beginning = -1;

#ifdef WIN32

// any way to check for open file descriptors on Win32?
inline bool check_filedes(bool x) { return 0;     }
inline bool checkfilesleftopen()  { return false; }

#else // !WIN32

#define FILEDES_MAX 256

bool filedes_open[FILEDES_MAX];

bool check_filedes(bool report)
{
	bool allOk = true;

	// See how many file descriptors there are with values < 256
	for(int d = 0; d < FILEDES_MAX; ++d)
	{
		if(::fcntl(d, F_GETFD) != -1)
		{
			// File descriptor obviously exists
			if (report && !filedes_open[d])
			{
				struct stat st;
				if (fstat(d, &st) == 0)
				{
					int m = st.st_mode;
					#define flag(x) ((m & x) ? #x " " : "")
					BOX_FATAL("File descriptor " << d << 
						" left open (type == " <<
						flag(S_IFIFO) <<
						flag(S_IFCHR) <<
						flag(S_IFDIR) <<
						flag(S_IFBLK) <<
						flag(S_IFREG) <<
						flag(S_IFLNK) <<
						flag(S_IFSOCK) <<
						" or " << m << ")");
				}
				else
				{
					BOX_FATAL("File descriptor " << d << 
						" left open (and stat failed)");
				}
	
				allOk = false;
				
			}
			else if (!report)
			{
				filedes_open[d] = true;
			}
		}
		else 
		{
			if (report && filedes_open[d])
			{
				BOX_FATAL("File descriptor " << d << 
					" was open, now closed");
				allOk = false;
			}
			else
			{
				filedes_open[d] = false;
			}
		}
	}

	if (!report && allOk)
	{
		filedes_open_at_beginning = 0;
	}
	
	return !allOk;
}

bool checkfilesleftopen()
{
	if(filedes_open_at_beginning == -1)
	{
		// Not used correctly, pretend that there were things 
		// left open so this gets investigated
		BOX_FATAL("File descriptor test was not initialised");
		return true;
	}

	// Count the file descriptors open
	return check_filedes(true);
}

#endif

int main(int argc, char * const * argv)
{
	// Start memory leak testing
	MEMLEAKFINDER_START

	Logging::SetProgramName(BOX_MODULE);

#ifdef HAVE_GETOPT_H
	#ifdef NDEBUG
	int logLevel = Log::NOTICE; // need an int to do math with
	#else
	int logLevel = Log::INFO; // need an int to do math with
	#endif

	struct option longopts[] = 
	{
		{ "bbackupd-args",	required_argument, NULL, 'c' },
		{ "bbstored-args",	required_argument, NULL, 's' },
		{ "test-daemon-args",	required_argument, NULL, 'd' },
		{ NULL,			0,                 NULL,  0  }
	};
	
	int ch;
	
	while ((ch = getopt_long(argc, argv, "c:d:qs:t:vPTUV", longopts, NULL))
		!= -1)
	{
		switch(ch)
		{
			case 'c':
			{
				if (bbackupd_args.length() > 0)
				{
					bbackupd_args += " ";
				}
				bbackupd_args += optarg;
			}
			break;

			case 'd':
			{
				if (test_args.length() > 0)
				{
					test_args += " ";
				}
				test_args += optarg;
			}
			break;

			case 's':
			{
				bbstored_args += " ";
				bbstored_args += optarg;
			}
			break;

			#ifndef WIN32
			case 'P':
			{
				Console::SetShowPID(true);
			}
			break;
			#endif

			case 'q':
			{
				if(logLevel == Log::NOTHING)
				{
					BOX_FATAL("Too many '-q': "
						"Cannot reduce logging "
						"level any more");
					return 2;
				}
				logLevel--;
			}
			break;

			case 'v':
			{
				if(logLevel == Log::EVERYTHING)
				{
					BOX_FATAL("Too many '-v': "
						"Cannot increase logging "
						"level any more");
					return 2;
				}
				logLevel++;
			}
			break;

			case 'V':
			{
				logLevel = Log::EVERYTHING;
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

			case 'U':
			{
				Console::SetShowTime(true);
				Console::SetShowTimeMicros(true);
			}
			break;

			case '?':
			{
				fprintf(stderr, "Unknown option: '%c'\n",
					optopt);
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

	Logging::SetGlobalLevel((Log::Level)logLevel);

	argc -= optind - 1;
	argv += optind - 1;
#endif // HAVE_GETOPT_H

	// If there is more than one argument, then the test is doing something advanced, so leave it alone
	bool fulltestmode = (argc == 1);

	if(fulltestmode)
	{
		// banner
		BOX_NOTICE("Running test TEST_NAME in " MODE_TEXT " mode...");

		// Count open file descriptors for a very crude "files left open" test
		check_filedes(false);

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

		fflush(stdout);
		fflush(stderr);
		
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

			fflush(stdout);
			fflush(stderr);
		
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

