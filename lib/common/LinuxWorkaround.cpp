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
//		Name:    LinuxWorkaround.cpp
//		Purpose: Workarounds for Linux
//		Created: 2003/10/31
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>

#include <string>

#include "LinuxWorkaround.h"
#include "CommonException.h"

#include "MemLeakFindOn.h"

#ifdef PLATFORM_LINUX

// --------------------------------------------------------------------------
//
// Function
//		Name:    LinuxWorkaround_FinishDirentStruct(struct dirent *, const char *)
//		Purpose: Finishes off filling in a dirent structure, which Linux leaves incomplete.
//		Created: 2003/10/31
//
// --------------------------------------------------------------------------
void LinuxWorkaround_FinishDirentStruct(struct dirent *entry, const char *DirectoryName)
{
	// From man readdir under Linux:
	//
	// BUGS
    //   Field d_type is not implemented as  of  libc6  2.1  and  will  always  return
    //   DT_UNKNOWN (0).
	//
	// What kind of an OS is this?
	
	
	// Build filename of this entry
	std::string fn(DirectoryName);
	fn += '/';
	fn += entry->d_name;
	
	// Do a stat on it
	struct stat st;
	if(::lstat(fn.c_str(), &st) != 0)
	{
		THROW_EXCEPTION(CommonException, OSFileError)
	}
	
	// Fill in the d_type field.
	if(S_ISREG(st.st_mode))
	{
		entry->d_type = DT_REG;	
	}
	else if(S_ISDIR(st.st_mode))
	{
		entry->d_type = DT_DIR;
	}
	else if(S_ISLNK(st.st_mode))
	{
		entry->d_type = DT_LNK;
	}
	// otherwise leave it as we found it
}

#endif // PLATFORM_LINUX

