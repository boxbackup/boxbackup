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
//		Name:    ExcludeList.h
//		Purpose: General purpose exclusion list
//		Created: 28/1/04
//
// --------------------------------------------------------------------------

#ifndef EXCLUDELIST__H
#define EXCLUDELIST__H

#include <string>
#include <set>
#include <vector>

// avoid including regex.h in lots of places
#ifndef EXCLUDELIST_IMPLEMENTATION_REGEX_T_DEFINED
	typedef int regex_t;
#endif

#include "Archive.h"

// --------------------------------------------------------------------------
//
// Class
//		Name:    ExcludeList
//		Purpose: General purpose exclusion list
//		Created: 28/1/04
//
// --------------------------------------------------------------------------
class ExcludeList
{
public:
	ExcludeList();
	~ExcludeList();

	void Deserialize(Archive & rArchive);
	void Serialize(Archive & rArchive) const;

	void AddDefiniteEntries(const std::string &rEntries);
	void AddRegexEntries(const std::string &rEntries);

	// Add exceptions to the exclusions (takes ownership)
	void SetAlwaysIncludeList(ExcludeList *pAlwaysInclude);
	
	// Test function
	bool IsExcluded(const std::string &rTest) const;
	
	// Mainly for tests
	unsigned int SizeOfDefiniteList() const {return mDefinite.size();}
	unsigned int SizeOfRegexList() const
#ifndef PLATFORM_REGEX_NOT_SUPPORTED
		{return mRegex.size();}
#else
		{return 0;}
#endif

private:
	std::set<std::string> mDefinite;
#ifndef PLATFORM_REGEX_NOT_SUPPORTED
	std::vector<regex_t *> mRegex;
	std::vector<std::string> mRegexStr;	// save original regular expression string-based source for Serialize
#endif

	// For exceptions to the excludes
	ExcludeList *mpAlwaysInclude;
};

#endif // EXCLUDELIST__H

