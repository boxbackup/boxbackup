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
//		Name:    ExcludeList.cpp
//		Purpose: General purpose exclusion list
//		Created: 28/1/04
//
// --------------------------------------------------------------------------

#include "Box.h"

#ifndef PLATFORM_REGEX_NOT_SUPPORTED
	#include <regex.h>
	#define EXCLUDELIST_IMPLEMENTATION_REGEX_T_DEFINED
#endif

#include "ExcludeList.h"
#include "Utils.h"
#include "Configuration.h"

#include "MemLeakFindOn.h"

// --------------------------------------------------------------------------
//
// Function
//		Name:    ExcludeList::ExcludeList()
//		Purpose: Constructor. Generates an exclude list which will allow everything
//		Created: 28/1/04
//
// --------------------------------------------------------------------------
ExcludeList::ExcludeList()
	: mpAlwaysInclude(0)
{
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    ExcludeList::~ExcludeList()
//		Purpose: Destructor
//		Created: 28/1/04
//
// --------------------------------------------------------------------------
ExcludeList::~ExcludeList()
{
#ifndef PLATFORM_REGEX_NOT_SUPPORTED
	// free regex memory
	while(mRegex.size() > 0)
	{
		regex_t *pregex = mRegex.back();
		mRegex.pop_back();
		// Free regex storage, and the structure itself
		::regfree(pregex);
		delete pregex;
	}
#endif

	// Clean up exceptions list
	if(mpAlwaysInclude != 0)
	{
		delete mpAlwaysInclude;
		mpAlwaysInclude = 0;
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    ExcludeList::AddDefiniteEntries(const std::string &)
//		Purpose: Adds a number of definite entries to the exclude list -- ones which
//				 will be excluded if and only if the test string matches exactly.
//				 Uses the Configuration classes' multi-value conventions, with
//				 multiple entires in one string separated by Configuration::MultiValueSeparator
//		Created: 28/1/04
//
// --------------------------------------------------------------------------
void ExcludeList::AddDefiniteEntries(const std::string &rEntries)
{
	// Split strings up
	std::vector<std::string> ens;
	SplitString(rEntries, Configuration::MultiValueSeparator, ens);
	
	// Add to set of excluded strings
	for(std::vector<std::string>::const_iterator i(ens.begin()); i != ens.end(); ++i)
	{
		if(i->size() > 0)
		{
			mDefinite.insert(*i);
		}
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    ExcludeList::AddRegexEntries(const std::string &)
//		Purpose: Adds a number of regular expression entries to the exclude list -- 
//				 if the test expression matches any of these regex, it will be excluded.
//				 Uses the Configuration classes' multi-value conventions, with
//				 multiple entires in one string separated by Configuration::MultiValueSeparator
//		Created: 28/1/04
//
// --------------------------------------------------------------------------
void ExcludeList::AddRegexEntries(const std::string &rEntries)
{
#ifndef PLATFORM_REGEX_NOT_SUPPORTED

	// Split strings up
	std::vector<std::string> ens;
	SplitString(rEntries, Configuration::MultiValueSeparator, ens);
	
	// Create and add new regular expressions
	for(std::vector<std::string>::const_iterator i(ens.begin()); i != ens.end(); ++i)
	{
		if(i->size() > 0)
		{
			// Allocate memory
			regex_t *pregex = new regex_t;
			
			try
			{
				// Compile
				if(::regcomp(pregex, i->c_str(), REG_EXTENDED | REG_NOSUB) != 0)
				{
					THROW_EXCEPTION(CommonException, BadRegularExpression)
				}
				
				// Store in list of regular expressions
				mRegex.push_back(pregex);
			}
			catch(...)
			{
				delete pregex;
				throw;
			}
		}
	}

#else
	THROW_EXCEPTION(CommonException, RegexNotSupportedOnThisPlatform)
#endif
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    ExcludeList::IsExcluded(const std::string &)
//		Purpose: Returns true if the entry should be excluded
//		Created: 28/1/04
//
// --------------------------------------------------------------------------
bool ExcludeList::IsExcluded(const std::string &rTest) const
{
	// Check against the always include list
	if(mpAlwaysInclude != 0)
	{
		if(mpAlwaysInclude->IsExcluded(rTest))
		{
			// Because the "always include" list says it's 'excluded'
			// this means it should actually be included.
			return false;
		}
	}

	// Is it in the set of definite entries?
	if(mDefinite.find(rTest) != mDefinite.end())
	{
		return true;
	}
	
	// Check against regular expressions
#ifndef PLATFORM_REGEX_NOT_SUPPORTED
	for(std::vector<regex_t *>::const_iterator i(mRegex.begin()); i != mRegex.end(); ++i)
	{
		// Test against this expression
		if(regexec(*i, rTest.c_str(), 0, 0 /* no match information required */, 0 /* no flags */) == 0)
		{
			// match happened
			return true;
		}
		// In all other cases, including an error, just continue to the next expression
	}
#endif

	return false;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    ExcludeList::SetAlwaysIncludeList(ExcludeList *)
//		Purpose: Takes ownership of the list, deletes any pre-existing list.
//				 NULL is acceptable to delete the list.
//				 The AlwaysInclude list is a list of exceptions to the exclusions.
//		Created: 19/2/04
//
// --------------------------------------------------------------------------
void ExcludeList::SetAlwaysIncludeList(ExcludeList *pAlwaysInclude)
{
	// Delete old list
	if(mpAlwaysInclude != 0)
	{
		delete mpAlwaysInclude;
		mpAlwaysInclude = 0;
	}
	
	// Store the pointer
	mpAlwaysInclude = pAlwaysInclude;
}


	


