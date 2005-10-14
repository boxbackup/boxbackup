// --------------------------------------------------------------------------
//
// File
//		Name:    BackupClientMakeExcludeList.cpp
//		Purpose: Makes exclude lists from bbbackupd config location entries
//		Created: 28/1/04
//
// --------------------------------------------------------------------------

#include "Box.h"

#include "BackupClientMakeExcludeList.h"
#include "Configuration.h"
#include "ExcludeList.h"

#include "MemLeakFindOn.h"


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupClientMakeExcludeList(const Configuration &, const char *, const char *)
//		Purpose: Given a Configuration object corresponding to a bbackupd Location, and the
//				 two names of the keys for definite and regex entries, return a ExcludeList.
//				 Or 0 if it isn't required.
//		Created: 28/1/04
//
// --------------------------------------------------------------------------
ExcludeList *BackupClientMakeExcludeList(const Configuration &rConfig, const char *DefiniteName, const char *RegexName,
	const char *AlwaysIncludeDefiniteName, const char *AlwaysIncludeRegexName)
{
	// Check that at least one of the entries exists
	if(!rConfig.KeyExists(DefiniteName) && !rConfig.KeyExists(RegexName))
	{
		// Neither exists -- return 0 as an Exclude list isn't required.
		return 0;
	}
	
	// Create the exclude list
	ExcludeList *pexclude = new ExcludeList;

	try
	{
		// Definite names to add?
		if(rConfig.KeyExists(DefiniteName))
		{
			pexclude->AddDefiniteEntries(rConfig.GetKeyValue(DefiniteName));
		}
		// Regular expressions to add?
		if(rConfig.KeyExists(RegexName))
		{
			pexclude->AddRegexEntries(rConfig.GetKeyValue(RegexName));
		}
		
		// Add a "always include" list?
		if(AlwaysIncludeDefiniteName != 0 && AlwaysIncludeRegexName != 0)
		{
			// This will accept NULL as a valid argument, so safe to do this.
			pexclude->SetAlwaysIncludeList(
					BackupClientMakeExcludeList(rConfig, AlwaysIncludeDefiniteName, AlwaysIncludeRegexName)
				);
		}
	}
	catch(...)
	{
		// Clean up
		delete pexclude;
		throw;
	}

	return pexclude;
}



