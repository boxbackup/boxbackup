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
//		Name:    BoxTimeToISO8601String(box_time_t)
//		Purpose: Convert a 64 bit box time to a ISO 8601 complient string
//		Created: 2003/10/10
//
// --------------------------------------------------------------------------
std::string BoxTimeToISO8601String(box_time_t Time)
{
	time_t timeInSecs = (time_t)BoxTimeToSeconds(Time);
	struct tm time;
	gmtime_r(&timeInSecs, &time);
	
	char str[128];	// more than enough space
	sprintf(str, "%04d-%02d-%02dT%02d:%02d:%02d", time.tm_year + 1900,
		time.tm_mon + 1, time.tm_mday, time.tm_hour, time.tm_min, time.tm_sec);
		
	return std::string(str);
}


