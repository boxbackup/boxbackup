// --------------------------------------------------------------------------
//
// File
//		Name:    WAFLocale_jp.h
//		Purpose: Japanese locale object
//		Created: 18/03/05
//
// --------------------------------------------------------------------------

#ifndef WAFLOCALE_en__H
#define WAFLOCALE_en__H

#include "WAFLocale.h"

// --------------------------------------------------------------------------
//
// Class
//		Name:    WAFLocale_jp
//		Purpose: Japanese locale object
//		Created: 18/03/05
//
// --------------------------------------------------------------------------
class WAFLocale_jp : public WAFLocale
{
public:
	WAFLocale_jp();
	virtual ~WAFLocale_jp();

	const char** GetMonthList() const;
	const char* GetMonthName(int MonthNumber) const;
	virtual void FormatDate(int Style, int Year, int Month, int Day, std::string &rFormattedOut) const;
};

#endif // WAFLOCALE_en__H

