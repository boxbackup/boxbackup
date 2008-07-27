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
			BOX_LOG_SYS_ERROR("Failed to gettimeofday(), "
				"dropping precision");
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

std::string FormatTime(box_time_t time, bool showMicros)
{
	std::ostringstream buf;

	time_t seconds = BoxTimeToSeconds(time);
	int micros = BoxTimeToMicroSeconds(time) % MICRO_SEC_IN_SEC;

	struct tm tm_now, *tm_ptr = &tm_now;

	#ifdef WIN32
		if ((tm_ptr = localtime(&seconds)) != NULL)
	#else
		if (localtime_r(&seconds, &tm_now) != NULL)
	#endif
	{
		buf << std::setfill('0') <<
			std::setw(2) << tm_ptr->tm_hour << ":" << 
			std::setw(2) << tm_ptr->tm_min  << ":" <<
			std::setw(2) << tm_ptr->tm_sec;

		if (showMicros)
		{
			buf << "." << std::setw(6) << micros;
		}
	}
	else
	{
		buf << strerror(errno);
	}

	return buf.str();
}

