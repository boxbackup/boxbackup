// --------------------------------------------------------------------------
//
// File
//		Name:    WAFLocale_nl.h
//		Purpose: Dutch locale object
//		Created: 23/11/04
//
// --------------------------------------------------------------------------

#ifndef WAFLocale_nl__H
#define WAFLocale_nl__H

#include "WAFLocale.h"

// --------------------------------------------------------------------------
//
// Class
//		Name:    WAFLocale_nl
//		Purpose: Dutch locale object
//		Created: 23/11/04
//
// --------------------------------------------------------------------------
class WAFLocale_nl : public WAFLocale
{
public:
	WAFLocale_nl();
	virtual ~WAFLocale_nl();

	const char** GetMonthList() const;
	const char* GetMonthName(int MonthNumber) const;
};

#endif // WAFLocale_nl__H

