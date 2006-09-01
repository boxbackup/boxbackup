// --------------------------------------------------------------------------
//
// File
//		Name:    WAFLocale_jp.cpp
//		Purpose: Japanese locale object
//		Created: 18/03/05
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <stdio.h>

#include "WAFLocale_jp.h"

#include "MemLeakFindOn.h"

static const char* en_months[] = {
	"UNDEFINED",
	"1月",
	"2月",
	"3月",
	"4月",
	"5月",
	"6月",
	"7月",
	"8月",
	"9月",
	"10月",
	"11月",
	"12月"
};


// --------------------------------------------------------------------------
//
// Function
//		Name:    WAFLocale_jp::WAFLocale_jp()
//		Purpose: Default constructor
//		Created: 18/03/05
//
// --------------------------------------------------------------------------
WAFLocale_jp::WAFLocale_jp()
{
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    WAFLocale_jp::~WAFLocale_jp()
//		Purpose: Default destructor
//		Created: 18/03/05
//
// --------------------------------------------------------------------------
WAFLocale_jp::~WAFLocale_jp()
{
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    WAFLocale_jp::GetMonthList()
//		Purpose: As base class
//		Created: 18/03/05
//
// --------------------------------------------------------------------------
const char** WAFLocale_jp::GetMonthList() const
{
	return en_months;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    WAFLocale_jp::GetMonthName(int)
//		Purpose: As base class
//		Created: 18/03/05
//
// --------------------------------------------------------------------------
const char* WAFLocale_jp::GetMonthName(int MonthNumber) const
{
	if(MonthNumber < 1 || MonthNumber > WAFLOCALE_MONTHS_IN_YEAR)
	{
		return en_months[0];
	}
	return en_months[MonthNumber];
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    WAFLocale_jp::FormatDate(int, int, int, int, std::string &)
//		Purpose: Override base class formatting
//		Created: 23/11/04
//
// --------------------------------------------------------------------------
void WAFLocale_jp::FormatDate(int Style, int Year, int Month, int Day, std::string &rFormattedOut) const
{
	// Both styles are the same in this locale
	char str[64];	// know maximum length
	::sprintf(str, "%04d年%d月%d日", Year, Month, Day);
	rFormattedOut = str;
}


