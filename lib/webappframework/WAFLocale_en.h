// --------------------------------------------------------------------------
//
// File
//		Name:    WAFLocale_en.h
//		Purpose: English (UK) locale object
//		Created: 23/11/04
//
// --------------------------------------------------------------------------

#ifndef WAFLOCALE_en__H
#define WAFLOCALE_en__H

#include "WAFLocale.h"

// --------------------------------------------------------------------------
//
// Class
//		Name:    WAFLocale_en
//		Purpose: English (UK) locale object
//		Created: 23/11/04
//
// --------------------------------------------------------------------------
class WAFLocale_en : public WAFLocale
{
public:
	WAFLocale_en();
	virtual ~WAFLocale_en();

	const char** GetMonthList() const;
	const char* GetMonthName(int MonthNumber) const;
};

#endif // WAFLOCALE_en__H

