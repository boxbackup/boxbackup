// distribution boxbackup-0.09
// 
//  
// Copyright (c) 2003, 2004
//      Ben Summers.  All rights reserved.
//  
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
// 1. Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
// 3. All use of this software and associated advertising materials must 
//    display the following acknowledgement:
//        This product includes software developed by Ben Summers.
// 4. The names of the Authors may not be used to endorse or promote
//    products derived from this software without specific prior written
//    permission.
// 
// [Where legally impermissible the Authors do not disclaim liability for 
// direct physical injury or death caused solely by defects in the software 
// unless it is modified by a third party.]
// 
// THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
// IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT,
// INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
// HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
// ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//  
//  
//  
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



