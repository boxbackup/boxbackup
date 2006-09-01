// --------------------------------------------------------------------------
//
// File
//		Name:    WAFLocale.h
//		Purpose: Base class for locales
//		Created: 23/11/04
//
// --------------------------------------------------------------------------

#ifndef WAFLOCALE__H
#define WAFLOCALE__H

#include <string>

#define WAFLOCALE_MONTHS_IN_YEAR		12

// --------------------------------------------------------------------------
//
// Class
//		Name:    WAFLocale
//		Purpose: Base class for locales
//		Created: 23/11/04
//
// --------------------------------------------------------------------------
class WAFLocale
{
public:
	WAFLocale();
	virtual ~WAFLocale();
	
	// --------------------------------------------------------------------------
	//
	// Function
	//		Name:    WAFLocale::GetMonthList()
	//		Purpose: Return an array of pointers to month names, for accessing
	//				 using a 1 based index (ie as per human readable dates).
	//		Created: 23/11/04
	//
	// --------------------------------------------------------------------------
	virtual const char** GetMonthList() const = 0; // use 1 -- 12 as index on array


	// --------------------------------------------------------------------------
	//
	// Function
	//		Name:    WAFLocale::GetMonthName(int)
	//		Purpose: Return a pointer to a month name, using 1 based index.
	//				 If the month is invalid, a string will still be returned
	//				 but it's text is undefined.
	//		Created: 23/11/04
	//
	// --------------------------------------------------------------------------
	virtual const char* GetMonthName(int MonthNumber) const = 0; // 1 -- 12

	// Date formatting styles
	enum
	{
		DateFormatLong = 0,
		DateFormatShort = 1,
		DateTimeFormatLong = 2,
		DateTimeFormatShort = 3
	};

	// Time format
	enum
	{
		TimeFormatUNIXEpoch = 0,
		TimeFormatYYYYMMDD = 1			// ie y*10000+m*100+d
	};

	// --------------------------------------------------------------------------
	//
	// Function
	//		Name:    WAFLocale::FormatDateTime(int, int, int, std::string &)
	//		Purpose: Format a date and/or time in the specifed style.
	//		Created: 23/11/04
	//
	// --------------------------------------------------------------------------
	virtual void FormatDateTime(int Style, int TimeFormat, int DateTime, std::string &rFormattedOut) const;

	// Slightly different name to avoid being hidden by derived classes
	inline std::string FormatDateTimeR(int Style, int TimeFormat, int DateTime) const
	{
		std::string r;
		FormatDateTime(Style, TimeFormat, DateTime, r);
		return r;
	}

	// --------------------------------------------------------------------------
	//
	// Function
	//		Name:    WAFLocale::FormatDate(int, int, int, int, std::string &)
	//		Purpose: Format a date in the specifed style.
	//		Created: 23/11/04
	//
	// --------------------------------------------------------------------------
	virtual void FormatDate(int Style, int Year, int Month, int Day, std::string &rFormattedOut) const;
	
	// Slightly different name to avoid being hidden by derived classes
	inline std::string FormatDateR(int Style, int Year, int Month, int Day) const
	{
		std::string r;
		FormatDate(Style, Year, Month, Day, r);
		return r;
	}
};

#endif // WAFLOCALE__H

