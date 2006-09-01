// --------------------------------------------------------------------------
//
// File
//		Name:    WAFLocale.cpp
//		Purpose: Base class for locales
//		Created: 23/11/04
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <stdio.h>
#include <time.h>

#include "WAFLocale.h"
#include "autogen_WebAppFrameworkException.h"

#include "MemLeakFindOn.h"


// --------------------------------------------------------------------------
//
// Function
//		Name:    WAFLocale::WAFLocale()
//		Purpose: Default constructor
//		Created: 23/11/04
//
// --------------------------------------------------------------------------
WAFLocale::WAFLocale()
{
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    WAFLocale::~WAFLocale()
//		Purpose: Default destructor
//		Created: 23/11/04
//
// --------------------------------------------------------------------------
WAFLocale::~WAFLocale()
{
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    WAFLocale::FormatDate(int, int, int, int, std::string &)
//		Purpose: Default date formatter, using dervied classes' month names.
//		Created: 23/11/04
//
// --------------------------------------------------------------------------
void WAFLocale::FormatDate(int Style, int Year, int Month, int Day, std::string &rFormattedOut) const
{
	int checkedMonth = Month;
	if(Month < 1 || Month > WAFLOCALE_MONTHS_IN_YEAR) checkedMonth = 0;
	char str[64];	// know maximum length

	switch(Style)
	{
	case WAFLocale::DateFormatLong:
		{
			::sprintf(str, "%d %s %d", Day, GetMonthName(checkedMonth), Year);
			rFormattedOut = str;
		}
		break;
	case WAFLocale::DateFormatShort:
		{
			::sprintf(str, "%02d/%02d/%02d", Day, Month, Year % 100);
			rFormattedOut = str;
		}
		break;
	default:
		THROW_EXCEPTION(WebAppFrameworkException, BadDateFormat);
		break;
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    WAFLocale::FormatDateTime(int, int, int, std::string &)
//		Purpose: 
//		Created: 9/1/05
//
// --------------------------------------------------------------------------
void WAFLocale::FormatDateTime(int Style, int TimeFormat, int DateTime, std::string &rFormattedOut) const
{
	// Convert to elements
	int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
	switch(TimeFormat)
	{
	case WAFLocale::TimeFormatUNIXEpoch:
		{
			time_t clock = DateTime;
			struct tm *decodedTime = ::gmtime(&clock);
			if(time != 0)
			{
				year = decodedTime->tm_year + 1900;
				month = decodedTime->tm_mon + 1;
				day = decodedTime->tm_mday;
				hour = decodedTime->tm_hour;
				minute = decodedTime->tm_min;
				second = decodedTime->tm_sec;
			}
		}
		break;
	case WAFLocale::TimeFormatYYYYMMDD:
		{
			year = DateTime / 10000;
			month = (DateTime / 100) % 100;
			day = DateTime % 100;
		}
		break;
	default:
		THROW_EXCEPTION(WebAppFrameworkException, BadTimeFormat);
		break;
	}
	
	// Format the result
	switch(Style)
	{
	case WAFLocale::DateFormatLong:
	case WAFLocale::DateFormatShort:
		// Delegate to formatter
		FormatDate(Style, year, month, day, rFormattedOut);
		break;
	case WAFLocale::DateTimeFormatLong:
		{
			FormatDate(WAFLocale::DateFormatLong, year, month, day, rFormattedOut);
			char str[64];
			::sprintf(str, ", %02d:%02d", hour, minute);
			rFormattedOut += str;
		}
		break;
	case WAFLocale::DateTimeFormatShort:
		{
			FormatDate(WAFLocale::DateFormatShort, year, month, day, rFormattedOut);
			char str[64];
			::sprintf(str, ", %02d:%02d", hour, minute);
			rFormattedOut += str;
		}
		break;
	default:
		THROW_EXCEPTION(WebAppFrameworkException, BadDateFormat);
		break;
	}
}


