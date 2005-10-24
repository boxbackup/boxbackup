// --------------------------------------------------------------------------
//
// File
//		Name:    AssertFailed.cpp
//		Purpose: Assert failure code
//		Created: 2003/09/04
//
// --------------------------------------------------------------------------

#ifndef NDEBUG

#include "Box.h"

#include <stdio.h>
#include <syslog.h>

#include "MemLeakFindOn.h"

bool AssertFailuresToSyslog = false;

void BoxDebugAssertFailed(char *cond, char *file, int line)
{
	printf("ASSERT FAILED: [%s] at %s(%d)\n", cond, file, line);
	if(AssertFailuresToSyslog)
	{
		::syslog(LOG_ERR, "ASSERT FAILED: [%s] at %s(%d)\n", cond, file, line);
	}
}


#endif // NDEBUG

