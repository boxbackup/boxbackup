// --------------------------------------------------------------------------
//
// File
//		Name:    BoxTime.cpp
//		Purpose: Time for the box
//		Created: 2003/10/08
//
// --------------------------------------------------------------------------

#include "Box.h"

#ifdef HAVE_SYS_TIME_H
	#include <sys/time.h>
#endif

#ifdef HAVE_TIME_H
	#include <time.h>
#endif

#include <errno.h>
#include <string.h>

#include "BoxTime.h"

#include "MemLeakFindOn.h"

// --------------------------------------------------------------------------
//
// Function
//		Name:    GetCurrentBoxTime()
//		Purpose: Returns the current time as a box time. 
//			 (1 sec precision, or better if supported by system)
//		Created: 2003/10/08
//
// --------------------------------------------------------------------------
box_time_t GetCurrentBoxTime()
{
	#ifdef HAVE_GETTIMEOFDAY
		struct timeval tv;
		if (gettimeofday(&tv, NULL) != 0)
		{
			BOX_ERROR("Failed to gettimeofday(), dropping "
				"precision: " << strerror(errno));
		}
		else
		{
			box_time_t timeNow = (tv.tv_sec * MICRO_SEC_IN_SEC_LL)
				+ tv.tv_usec;
			return timeNow;
		}
	#endif
	
	return SecondsToBoxTime(time(0));
}
