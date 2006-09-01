// --------------------------------------------------------------------------
//
// File
//		Name:    WAFLocale_en.cpp
//		Purpose: English (UK) locale object
//		Created: 23/11/04
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <stdio.h>

#include "WAFLocale_en.h"

#include "MemLeakFindOn.h"

static const char* en_months[] = {
	"UNDEFINED",
	"January",
	"February",
	"March",
	"April",
	"May",
	"June",
	"July",
	"August",
	"September",
	"October",
	"November",
	"December"
};


// --------------------------------------------------------------------------
//
// Function
//		Name:    WAFLocale_en::WAFLocale_en()
//		Purpose: Default constructor
//		Created: 23/11/04
//
// --------------------------------------------------------------------------
WAFLocale_en::WAFLocale_en()
{
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    WAFLocale_en::~WAFLocale_en()
//		Purpose: Default destructor
//		Created: 23/11/04
//
// --------------------------------------------------------------------------
WAFLocale_en::~WAFLocale_en()
{
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    WAFLocale_en::GetMonthList()
//		Purpose: As base class
//		Created: 23/11/04
//
// --------------------------------------------------------------------------
const char** WAFLocale_en::GetMonthList() const
{
	return en_months;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    WAFLocale_en::GetMonthName(int)
//		Purpose: As base class
//		Created: 23/11/04
//
// --------------------------------------------------------------------------
const char* WAFLocale_en::GetMonthName(int MonthNumber) const
{
	if(MonthNumber < 1 || MonthNumber > WAFLOCALE_MONTHS_IN_YEAR)
	{
		return en_months[0];
	}
	return en_months[MonthNumber];
}


