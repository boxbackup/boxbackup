// --------------------------------------------------------------------------
//
// File
//		Name:    WAFLocale_nl.cpp
//		Purpose: Dutch locale object
//		Created: 23/11/04
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <stdio.h>

#include "WAFLocale_nl.h"

#include "MemLeakFindOn.h"

// NOTE: Dutch people do not capitalise their month names
static const char* en_months[] = {
	"UNDEFINED",
	"januari",
	"februari",
	"maart",
	"april",
	"mei",
	"juni",
	"juli",
	"augustus",
	"september",
	"oktober",
	"november",
	"december"
};


// --------------------------------------------------------------------------
//
// Function
//		Name:    WAFLocale_nl::WAFLocale_nl()
//		Purpose: Default constructor
//		Created: 23/11/04
//
// --------------------------------------------------------------------------
WAFLocale_nl::WAFLocale_nl()
{
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    WAFLocale_nl::~WAFLocale_nl()
//		Purpose: Default destructor
//		Created: 23/11/04
//
// --------------------------------------------------------------------------
WAFLocale_nl::~WAFLocale_nl()
{
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    WAFLocale_nl::GetMonthList()
//		Purpose: As base class
//		Created: 23/11/04
//
// --------------------------------------------------------------------------
const char** WAFLocale_nl::GetMonthList() const
{
	return en_months;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    WAFLocale_nl::GetMonthName(int)
//		Purpose: As base class
//		Created: 23/11/04
//
// --------------------------------------------------------------------------
const char* WAFLocale_nl::GetMonthName(int MonthNumber) const
{
	if(MonthNumber < 1 || MonthNumber > WAFLOCALE_MONTHS_IN_YEAR)
	{
		return en_months[0];
	}
	return en_months[MonthNumber];
}


