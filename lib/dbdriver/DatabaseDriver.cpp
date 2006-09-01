// --------------------------------------------------------------------------
//
// File
//		Name:    DatabaseDriver.cpp
//		Purpose: Database driver interface
//		Created: 2/5/04
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <syslog.h>

#include "DatabaseDriver.h"
#include "autogen_DatabaseException.h"

#include "MemLeakFindOn.h"


// --------------------------------------------------------------------------
//
// Function
//		Name:    DatabaseDriver::DatabaseDriver()
//		Purpose: Constructor
//		Created: 2/5/04
//
// --------------------------------------------------------------------------
DatabaseDriver::DatabaseDriver()
{
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DatabaseDriver::~DatabaseDriver()
//		Purpose: Destructor
//		Created: 2/5/04
//
// --------------------------------------------------------------------------
DatabaseDriver::~DatabaseDriver()
{
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DatabaseDriver::ReportErrorMessage(const char *)
//		Purpose: Report an error message from a driver
//		Created: 10/5/04
//
// --------------------------------------------------------------------------
void DatabaseDriver::ReportErrorMessage(const char *ErrorMessage)
{
	if(ErrorMessage != 0)
	{
		TRACE2("%s error: %s\n", GetDriverName(), ErrorMessage);
		::syslog(LOG_ERR, "%s error: %s\n", GetDriverName(), ErrorMessage);
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DatabaseDriver::TranslateGeneric(int, const char **, int *, std::string &)
//		Purpose: Translate a generic represetation (overrideable)
//		Created: 15/5/04
//
// --------------------------------------------------------------------------
void DatabaseDriver::TranslateGeneric(int NumberElements, const char **Element, int *ElementLength, std::string &rOut)
{
	const TranslateMap_t &translate(GetGenericTranslations());
	
	// Find the element in the map
	std::string nm(Element[0], ElementLength[0]);
	if(NumberElements > 1)
	{
		nm += '0' + (NumberElements - 1);
	}

	// Find the corresponding string
	TranslateMap_t::const_iterator e(translate.find(nm));
	if(e == translate.end())
	{
		// Not found
		THROW_EXCEPTION(DatabaseException, GenericNotKnownByDriver)
	}

	// Output it!
	if(NumberElements <= 1)
	{
		// Just copy into output, don't bother with looking for arguments
		rOut = e->second;
		return;
	}
	
	// Copy into output, adding in elements where required
	int b = 0;
	int p = 0;
	rOut = std::string(); // ie clear()
	const char *t = e->second.c_str();
	int t_size = e->second.size();
	while(p < t_size)
	{
		if(t[p] == '!')
		{
			// Output string so far
			if(b != p)
			{
				rOut.append(t + b, p - b);
			}
			
			// Find argument number
			++p;
			int a = t[p] - '0';
			if(a < 0 || a > NumberElements)
			{
				THROW_EXCEPTION(DatabaseException, BadGenericTranslation)
			}
			// ... and append the argument value
			rOut.append(Element[a + 1], ElementLength[a + 1]);
			
			// Set new beginning
			b = p + 1;
		}
		++p;
	}
	// Anything remaining
	if(b != p)
	{
		rOut.append(t + b, p - b);
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DatabaseDriver::GetGenericTranslations()
//		Purpose: Return the map of generic translations for this driver
//		Created: 15/5/04
//
// --------------------------------------------------------------------------
const DatabaseDriver::TranslateMap_t &DatabaseDriver::GetGenericTranslations()
{
	// Default implementation just returns a map containing nothing
	static const TranslateMap_t nullMap;
	return nullMap;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DatabaseDrvQuery::DatabaseDrvQuery()
//		Purpose: Constructor
//		Created: 7/5/04
//
// --------------------------------------------------------------------------
DatabaseDrvQuery::DatabaseDrvQuery()
{
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DatabaseDrvQuery::~DatabaseDrvQuery()
//		Purpose: Destructor
//		Created: 7/5/04
//
// --------------------------------------------------------------------------
DatabaseDrvQuery::~DatabaseDrvQuery()
{
}

