// --------------------------------------------------------------------------
//
// File
//		Name:    WAFLocale_it.h
//		Purpose: Italian locale object
//		Created: 18/03/05
//
// --------------------------------------------------------------------------

#ifndef WAFLOCALE_en__H
#define WAFLOCALE_en__H

#include "WAFLocale.h"

// --------------------------------------------------------------------------
//
// Class
//		Name:    WAFLocale_it
//		Purpose: English (UK) locale object
//		Created: 18/03/05
//
// --------------------------------------------------------------------------
class WAFLocale_it : public WAFLocale
{
public:
	WAFLocale_it();
	virtual ~WAFLocale_it();

	const char** GetMonthList() const;
	const char* GetMonthName(int MonthNumber) const;
};

#endif // WAFLOCALE_en__H

