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

std::string FormatTime(box_time_t time, bool includeDate, bool showMicros)
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
		buf << std::setfill('0');

		if (includeDate)
		{
			buf << 	std::setw(4) << (tm_ptr->tm_year + 1900) << "-" <<
				std::setw(2) << (tm_ptr->tm_mon  + 1) << "-" <<
				std::setw(2) << (tm_ptr->tm_mday) << " ";
		}

		buf <<	std::setw(2) << tm_ptr->tm_hour << ":" << 
			std::setw(2) << tm_ptr->tm_min  << ":" <<
			std::setw(2) << tm_ptr->tm_sec;

		if (showMicros)
		{
			buf << "." << std::setw(3) << (int)(micros / 1000);
		}
	}
	else
	{
		buf << strerror(errno);
	}

	return buf.str();
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    ShortSleep(box_time_t duration)
//		Purpose: Sleeps for the specified duration as accurately
//			 and efficiently as possible.
//		Created: 2011/01/11
//
// --------------------------------------------------------------------------

void ShortSleep(box_time_t duration, bool logDuration)
{
	if(logDuration)
	{
		BOX_TRACE("Sleeping for " << BOX_FORMAT_MICROSECONDS(duration));
	}

#ifdef WIN32
	Sleep(BoxTimeToMilliSeconds(duration));
#else
	struct timespec ts;
	memset(&ts, 0, sizeof(ts));
	ts.tv_sec  = duration / MICRO_SEC_IN_SEC;
	ts.tv_nsec = (duration % MICRO_SEC_IN_SEC) * 1000;

	box_time_t start_time = GetCurrentBoxTime();

	while (nanosleep(&ts, &ts) == -1 && errno == EINTR)
	{
		// FIXME evil hack for OSX, where ts.tv_sec contains
		// a negative number interpreted as unsigned 32-bit
		// when nanosleep() returns later than expected.

		int32_t secs = (int32_t) ts.tv_sec;
		int64_t remain_ns = ((int64_t)secs * 1000000000) + ts.tv_nsec;

		if (remain_ns < 0)
		{
			BOX_WARNING("nanosleep interrupted " <<
				((float)(0 - remain_ns) / 1000000000) <<
				" secs late");
			return;
		}

		BOX_TRACE("nanosleep interrupted with " << remain_ns <<
			" nanosecs remaining, sleeping again");
	}

	box_time_t sleep_time = GetCurrentBoxTime() - start_time;	
	BOX_TRACE("Actually slept for " << BOX_FORMAT_MICROSECONDS(sleep_time) <<
		", was aiming for " << BOX_FORMAT_MICROSECONDS(duration));
#endif
}

