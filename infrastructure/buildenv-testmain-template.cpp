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

#ifdef HAVE_NETDB_H
#	include <netdb.h>
#endif

#ifdef HAVE_SYS_SOCKET_H
#	include <sys/socket.h>
#endif

#include <sys/stat.h>
#include <sys/types.h>

#ifdef HAVE_SYS_UN_H
#	include <sys/un.h>
#endif

#include <exception>
#include <iostream>
#include <list>
#include <string>

#ifdef HAVE_OPENSSL_CLEANUP
#	include <openssl/crypto.h>
#endif

#include "box_getopt.h"
#include "depot.h"
#include "Logging.h"
#include "Test.h"
#include "Timer.h"

#include "MemLeakFindOn.h"

int test(int argc, const char *argv[]);

#ifdef BOX_RELEASE_BUILD
	#define MODE_TEXT	"release"
#else
	#define MODE_TEXT	"debug"
#endif

#ifdef WIN32
	#define QUIET_PROCESS "-Q"
#else
	#define QUIET_PROCESS ""
#endif

std::string bbackupd_args = QUIET_PROCESS,
	bbstored_args = QUIET_PROCESS,
	bbackupquery_args,
	test_args;

bool filedes_initialised = false;

#ifdef WIN32

// any way to check for open file descriptors on Win32?
inline bool check_filedes(bool x) { return true; }
inline bool checkfilesleftopen()  { return true; }

#else // !WIN32

#define FILEDES_MAX 256

typedef enum
{
	OPEN,
	CLOSED,
	SYSLOG,
	STILLOPEN,
	LEAKED,
}
filedes_t;

filedes_t filedes_open[FILEDES_MAX];

bool check_filedes(bool report)
{
	bool allOk = true;

	// See how many file descriptors there are with values < 256.
	// In order to avoid disturbing things, we scan the file descriptors
	// first, marking the ones that were OPEN at startup (report == FALSE)
	// as STILLOPEN and the ones that were not as LEAKED. Then we run
	// through again and print the results.
	for(int d = 0; d < FILEDES_MAX; ++d)
	{
		if(::fcntl(d, F_GETFD) != -1)
		{
			// File descriptor obviously exists, but is it /dev/log?
			// Mark it as OPEN for now, and we'll find out later.
			if(report)
			{
				if(filedes_open[d] == OPEN)
				{
					filedes_open[d] = STILLOPEN;
				}
				else
				{
					filedes_open[d] = LEAKED;
				}
			}
			else
			{
				filedes_open[d] = OPEN;
			}
		}
		else
		{
			filedes_open[d] = CLOSED;
		}
	}

	if(!report)
	{
		filedes_initialised = true;
		return true;
	}

	// Now loop again, reporting differences.
	for(int d = 0; d < FILEDES_MAX; ++d)
	{
		if(filedes_open[d] != LEAKED)
		{
			continue;
		}

		bool stat_success = false;
		struct stat st;
		if(fstat(d, &st) == 0)
		{
			stat_success = true;

			if(st.st_mode & S_IFSOCK)
			{
				char buffer[256];
				socklen_t addrlen = sizeof(buffer);

#ifdef HAVE_GETPEERNAME
				if(getpeername(d, (sockaddr*)buffer, &addrlen) != 0)
				{
					BOX_LOG_SYS_WARNING("Failed to getpeername(" << 
						d << "), cannot identify /dev/log");
				}
				else
				{
					struct sockaddr_un *sa = 
						(struct sockaddr_un *)buffer;
					if(sa->sun_family == PF_UNIX &&
						!strcmp(sa->sun_path, "/dev/log"))
					{
						// it's a syslog socket, ignore it
						filedes_open[d] = SYSLOG;
					}
				}
#endif // HAVE_GETPEERNAME
			}
		}

		if(filedes_open[d] == SYSLOG)
		{
			// Different libcs have different ideas
			// about when to open and close this
			// socket, and it's not a leak, so
			// ignore it.
		}
		else if(stat_success)
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
			allOk = false;
		}
		else
		{
			BOX_FATAL("File descriptor " << d << 
				" left open (and stat failed)");
			allOk = false;
		}
	}

	if (!report && allOk)
	{
		filedes_initialised = true;
	}
	
	return allOk;
}

bool checkfilesleftopen()
{
	if(!filedes_initialised)
	{
		// Not used correctly, pretend that there were things 
		// left open so this gets investigated
		BOX_FATAL("File descriptor test was not initialised");
		return false;
	}

	// Count the file descriptors open
	return check_filedes(true);
}

#endif

int Usage(const std::string& ProgramName)
{
	std::cout << 
	"(built with QDBM " << dpversion << ")\n"
	"\n"
	"Usage: " << ProgramName << " [options]\n"
	"\n"
	"Options:\n"
	"  -c/--bbackupd-args <args>     Arguments to pass to bbackupd/BackupDaemon\n"
	"  -s/--bbstored-args <args>     Arguments to pass to bbstored/BackupStoreDaemon\n"
	"  -d/--test-daemon-args <args>  Arguments to pass to TestDaemon\n"
	"  -e/--execute-only <test>      Execute only specific named test, can repeat\n"
	"  -h/--help                     Show this command-line help\n"
	<< Logging::OptionParser::GetUsageString();
	return 0;
}

int main(int argc, char * const * argv)
{
	// Start memory leak testing
	MEMLEAKFINDER_START

	Logging::SetProgramName(BOX_MODULE);

	struct option longopts[] = 
	{
		{ "bbackupd-args",	required_argument, NULL, 'c' },
		{ "bbstored-args",	required_argument, NULL, 's' },
		{ "test-daemon-args",	required_argument, NULL, 'd' },
		{ "execute-only",	required_argument, NULL, 'e' },
		{ "help",		no_argument, NULL,       'h' },
		{ NULL,			0,                 NULL,  0  }
	};

	int c;
	std::string options("c:d:e:hs:");
	options += Logging::OptionParser::GetOptionString();
	Logging::OptionParser LogLevel;

	while ((c = getopt_long(argc, argv, options.c_str(), longopts, NULL))
		!= -1)
	{
		switch(c)
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

			case 'e':
			{
				run_only_named_tests.push_back(optarg);
			}
			break;

			case 'h':
			{
				return Usage(argv[0]);
			}
			break;

			case 's':
			{
				bbstored_args += " ";
				bbstored_args += optarg;
			}
			break;

			default:
			{
				int ret = LogLevel.ProcessOption(c);
				if(ret != 0)
				{
					fprintf(stderr, "Unknown option code "
						"'%c'\n", c);
					exit(2);
				}
			}
		}
	}

	Logging::FilterSyslog(Log::NOTHING);
	Logging::FilterConsole(LogLevel.GetCurrentLevel());

	argc -= optind - 1;
	argv += optind - 1;

	// If there is more than one argument, then the test is doing something advanced, so leave it alone
	bool fulltestmode = (argc == 1);

	if(fulltestmode)
	{
		// banner
		BOX_NOTICE("Running test TEST_NAME in " MODE_TEXT " mode...");

		// Count open file descriptors for a very crude "files left open" test
		Logging::GetSyslog().Shutdown();

		// On NetBSD, gethostbyname() appears to open a kqueue socket
		// and it's not clear how to close it again. So let's just do
		// it once, before counting fds for the first time, so that it's
		// already open and doesn't count as a leak.
		::gethostbyname("nonexistent");

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
		Timers::Cleanup(false);

		fflush(stdout);
		fflush(stderr);
		
		// check for memory leaks, if enabled
		#ifdef BOX_MEMORY_LEAK_TESTING
			if(memleakfinder_numleaks() != 0)
			{
				num_failures++;
				printf("FAILURE: Memory leaks detected in test code\n");
				printf("==== MEMORY LEAKS =================================\n");
				memleakfinder_reportleaks();
				printf("===================================================\n");
			}
		#endif
		
		if(fulltestmode)
		{
			Logging::GetSyslog().Shutdown();

			// On Ubuntu 18.04, initialising OpenSSL 1.1.1 leaves open file handles to
			// /dev/[u]random which are not easy to close (the docs for OPENSSL_cleanup
			// recommend not to call it), but we want to avoid detecting those as
			// leaking file descriptors
#ifdef HAVE_OPENSSL_CLEANUP
			OPENSSL_cleanup();
#endif

			bool filesleftopen = !checkfilesleftopen();

			fflush(stdout);
			fflush(stderr);
		
			if(filesleftopen)
			{
				num_failures++;
				printf("IMPLICIT TEST FAILED: Something left files open\n");
			}
			if(num_failures > 0)
			{
				printf("FAILED: %d tests failed (first at "
					"%s:%d)\n", num_failures, 
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
		if(!checkfilesleftopen())
		{
			printf("WARNING: Files were left open\n");
		}
	}
}

