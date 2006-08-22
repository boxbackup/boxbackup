// --------------------------------------------------------------------------
//
// File
//		Name:    BoxTime.cpp
//		Purpose: Time for the box
//		Created: 2003/10/08
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <time.h>
#include <sys/time.h>

#include "BoxTime.h"

#include "MemLeakFindOn.h"

// --------------------------------------------------------------------------
//
// Function
//		Name:    GetCurrentBoxTime()
//		Purpose: Returns the current time as a box time. (1 sec precision)
//		Created: 2003/10/08
//
// --------------------------------------------------------------------------
box_time_t GetCurrentBoxTime()
{
	struct timeval tv;
	int result = gettimeofday(&tv, NULL);
	if (result != 0)
	{
		TRACE1("Error: gettimeofday returned %d, approximating\n", result);
		return SecondsToBoxTime(time(0));
	}
	return ((uint64_t)tv.tv_sec * MICRO_SEC_IN_SEC_LL) + tv.tv_usec;
}


