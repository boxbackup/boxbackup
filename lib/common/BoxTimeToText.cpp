// --------------------------------------------------------------------------
//
// File
//		Name:    BoxTimeToText.cpp
//		Purpose: Convert box time to text
//		Created: 2003/10/10
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <sys/types.h>
#include <time.h>
#include <stdio.h>

#include "BoxTimeToText.h"

#include "MemLeakFindOn.h"

// --------------------------------------------------------------------------
//
// Function
//		Name:    BoxTimeToISO8601String(box_time_t, bool)
//		Purpose: Convert a 64 bit box time to a ISO 8601 compliant 
//			string, either in local or UTC time
//		Created: 2003/10/10
//
// --------------------------------------------------------------------------
std::string BoxTimeToISO8601String(box_time_t Time, bool localTime)
{
	time_t timeInSecs = BoxTimeToSeconds(Time);
	char str[128];	// more than enough space

#ifdef WIN32
	struct tm *time;
	__time64_t winTime = timeInSecs;

	if(localTime)
	{
		time = _localtime64(&winTime);
	}
	else
	{
		time = _gmtime64(&winTime);
	}

	if(time == NULL)
	{
		// ::sprintf(str, "%016I64x ", bob);
		return std::string("unable to convert time");
	}
	
	sprintf(str, "%04d-%02d-%02dT%02d:%02d:%02d", time->tm_year + 1900,
		time->tm_mon + 1, time->tm_mday, time->tm_hour, 
		time->tm_min, time->tm_sec);
#else // ! WIN32
	struct tm time;

	if(localTime)
	{
		localtime_r(&timeInSecs, &time);
	}
	else
	{
		gmtime_r(&timeInSecs, &time);
	}
	
	sprintf(str, "%04d-%02d-%02dT%02d:%02d:%02d", time.tm_year + 1900,
		time.tm_mon + 1, time.tm_mday, time.tm_hour, 
		time.tm_min, time.tm_sec);
#endif // WIN32
	
	return std::string(str);
}


