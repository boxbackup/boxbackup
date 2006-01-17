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
//		Name:    Utils.cpp
//		Purpose: Utility function
//		Created: 2003/07/31
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>

#ifdef SHOW_BACKTRACE_ON_EXCEPTION
	#include <execinfo.h>
	#include <stdlib.h>
#endif

#include "Utils.h"
#include "CommonException.h"

#include "MemLeakFindOn.h"

// --------------------------------------------------------------------------
//
// Function
//		Name:    SplitString(const std::string &, char, std::vector<std::string> &)
//		Purpose: Splits a string at a given character
//		Created: 2003/07/31
//
// --------------------------------------------------------------------------
void SplitString(const std::string &String, char SplitOn, std::vector<std::string> &rOutput)
{
	// Split it up.
	std::string::size_type b = 0;
	std::string::size_type e = 0;
	while(e = String.find_first_of(SplitOn, b), e != String.npos)
	{
		// Get this string
		unsigned int len = e - b;
		if(len >= 1)
		{
			rOutput.push_back(String.substr(b, len));
		}
		b = e + 1;
	}
	// Last string
	if(b < String.size())
	{
		rOutput.push_back(String.substr(b));
	}
/*#ifndef NDEBUG
	TRACE2("Splitting string '%s' on %c\n", String.c_str(), SplitOn);
	for(unsigned int l = 0; l < rOutput.size(); ++l)
	{
		TRACE2("%d = '%s'\n", l, rOutput[l].c_str());
	}
#endif*/
}

#ifdef SHOW_BACKTRACE_ON_EXCEPTION
void DumpStackBacktrace()
{
	void *array[10];
	size_t size;
	char **strings;
	size_t i;

	size = backtrace (array, 10);
	strings = backtrace_symbols (array, size);

	printf ("Obtained %zd stack frames.\n", size);

	for(i = 0; i < size; i++)
		printf("%s\n", strings[i]);

	free (strings);
}
#endif



// --------------------------------------------------------------------------
//
// Function
//		Name:    FileExists(const char *)
//		Purpose: Does a file exist?
//		Created: 20/11/03
//
// --------------------------------------------------------------------------
bool FileExists(const char *Filename, int64_t *pFileSize, bool TreatLinksAsNotExisting)
{
	struct stat st;
	if(::stat(Filename, &st) != 0)
	{
		if(errno == ENOENT)
		{
			return false;
		}
		else
		{
			THROW_EXCEPTION(CommonException, OSFileError);
		}
	}

	// is it a file?	
	if((st.st_mode & S_IFDIR) == 0)
	{
		if(TreatLinksAsNotExisting && ((st.st_mode & S_IFLNK) != 0))
		{
			return false;
		}
	
		// Yes. Tell caller the size?
		if(pFileSize != 0)
		{
			*pFileSize = st.st_size;
		}
	
		return true;
	}
	else
	{
		return false;
	}
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    ObjectExists(const char *)
//		Purpose: Does a object exist, and if so, is it a file or a directory?
//		Created: 23/11/03
//
// --------------------------------------------------------------------------
int ObjectExists(const char *Filename)
{
	struct stat st;
	if(::stat(Filename, &st) != 0)
	{
		if(errno == ENOENT)
		{
			return ObjectExists_NoObject;
		}
		else
		{
			THROW_EXCEPTION(CommonException, OSFileError);
		}
	}

	// is it a file or a dir?
	return ((st.st_mode & S_IFDIR) == 0)?ObjectExists_File:ObjectExists_Dir;
}




