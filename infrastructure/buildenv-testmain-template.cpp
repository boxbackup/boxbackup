// distribution boxbackup-0.09
// 
//  
// Copyright (c) 2003, 2004
//      Ben Summers.  All rights reserved.
//  
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
// 1. Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
// 3. All use of this software and associated advertising materials must 
//    display the following acknowledgement:
//        This product includes software developed by Ben Summers.
// 4. The names of the Authors may not be used to endorse or promote
//    products derived from this software without specific prior written
//    permission.
// 
// [Where legally impermissible the Authors do not disclaim liability for 
// direct physical injury or death caused solely by defects in the software 
// unless it is modified by a third party.]
// 
// THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
// IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT,
// INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
// HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
// ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//  
//  
//  
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

