// --------------------------------------------------------------------------
//
// File
//		Name:    WAFLocale_it.cpp
//		Purpose: Italian locale object
//		Created: 18/03/05
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <stdio.h>

#include "WAFLocale_it.h"

#include "MemLeakFindOn.h"

static const char* en_months[] = {
	"UNDEFINED",
	"Gennaio",
	"Febbraio",
	"Marzo",
	"Aprile",
	"Maggio",
	"Giugno",
	"Luglio",
	"Agosto",
	"Settembre",
	"Ottobre",
	"Novembre",
	"Dicembre"
};


// --------------------------------------------------------------------------
//
// Function
//		Name:    WAFLocale_it::WAFLocale_it()
//		Purpose: Default constructor
//		Created: 18/03/05
//
// --------------------------------------------------------------------------
WAFLocale_it::WAFLocale_it()
{
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    WAFLocale_it::~WAFLocale_it()
//		Purpose: Default destructor
//		Created: 18/03/05
//
// --------------------------------------------------------------------------
WAFLocale_it::~WAFLocale_it()
{
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    WAFLocale_it::GetMonthList()
//		Purpose: As base class
//		Created: 18/03/05
//
// --------------------------------------------------------------------------
const char** WAFLocale_it::GetMonthList() const
{
	return en_months;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    WAFLocale_it::GetMonthName(int)
//		Purpose: As base class
//		Created: 18/03/05
//
// --------------------------------------------------------------------------
const char* WAFLocale_it::GetMonthName(int MonthNumber) const
{
	if(MonthNumber < 1 || MonthNumber > WAFLOCALE_MONTHS_IN_YEAR)
	{
		return en_months[0];
	}
	return en_months[MonthNumber];
}


