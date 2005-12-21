// --------------------------------------------------------------------------
//
// File
//		Name:    BoxTimeToUnix.h
//		Purpose: Convert times in 64 bit values to UNIX structures
//		Created: 2003/10/07
//
// --------------------------------------------------------------------------

#ifndef FILEMODIFICATIONTIMETOTIMEVAL__H
#define FILEMODIFICATIONTIMETOTIMEVAL__H

#ifdef WIN32
#include <time.h>
#else
#include <sys/time.h>
#endif

#include "BoxTime.h"

inline void BoxTimeToTimeval(box_time_t Time, struct timeval &tv)
{
	tv.tv_sec = (long)(Time / MICRO_SEC_IN_SEC_LL);
	tv.tv_usec = (long)(Time % MICRO_SEC_IN_SEC_LL);
}

inline void BoxTimeToTimespec(box_time_t Time, struct timespec &tv)
{
	tv.tv_sec = (long)(Time / MICRO_SEC_IN_SEC_LL);
	tv.tv_nsec = ((long)(Time % MICRO_SEC_IN_SEC_LL)) * NANO_SEC_IN_USEC;
}

#endif // FILEMODIFICATIONTIMETOTIMEVAL__H

