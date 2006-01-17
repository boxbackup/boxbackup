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
//		Name:    RaidFileUtil.h
//		Purpose: Utilities for the raid file classes
//		Created: 2003/07/11
//
// --------------------------------------------------------------------------

#ifndef RAIDFILEUTIL__H
#define RAIDFILEUTIL__H

#include "RaidFileController.h"
#include "RaidFileException.h"

// note: these are hardcoded into the directory searching code
#define RAIDFILE_EXTENSION			".rf"
#define RAIDFILE_WRITE_EXTENSION	".rfw"

// --------------------------------------------------------------------------
//
// Class
//		Name:    RaidFileUtil
//		Purpose: Utility functions for RaidFile classes
//		Created: 2003/07/11
//
// --------------------------------------------------------------------------
class RaidFileUtil
{
public:
	typedef enum 
	{
		NoFile = 0,
		NonRaid = 1,
		AsRaid = 2,
		AsRaidWithMissingReadable = 3,
		AsRaidWithMissingNotRecoverable = 4
	} ExistType;
	
	typedef enum
	{
		Stripe1Exists = 1,
		Stripe2Exists = 2,
		ParityExists = 4
	};
	
	static ExistType RaidFileExists(RaidFileDiscSet &rDiscSet, const std::string &rFilename, int *pStartDisc = 0, int *pExisitingFiles = 0, int64_t *pRevisionID = 0);
	
	static int64_t DiscUsageInBlocks(int64_t FileSize, const RaidFileDiscSet &rDiscSet);
	
	// --------------------------------------------------------------------------
	//
	// Function
	//		Name:    std::string MakeRaidComponentName(RaidFileDiscSet &, const std::string &, int)
	//		Purpose: Returns the OS filename for a file of part of a disc set
	//		Created: 2003/07/11
	//
	// --------------------------------------------------------------------------	
	static inline std::string MakeRaidComponentName(RaidFileDiscSet &rDiscSet, const std::string &rFilename, int Disc)
	{
		if(Disc < 0 || Disc >= (int)rDiscSet.size())
		{
			THROW_EXCEPTION(RaidFileException, NoSuchDiscSet)
		}
		std::string r(rDiscSet[Disc]);
		r += DIRECTORY_SEPARATOR_ASCHAR;
		r += rFilename;
		r += RAIDFILE_EXTENSION;
		return r;
	}
	
	// --------------------------------------------------------------------------
	//
	// Function
	//		Name:    std::string MakeWriteFileName(RaidFileDiscSet &, const std::string &)
	//		Purpose: Returns the OS filename for the temporary write file
	//		Created: 2003/07/11
	//
	// --------------------------------------------------------------------------	
	static inline std::string MakeWriteFileName(RaidFileDiscSet &rDiscSet, const std::string &rFilename, int *pOnDiscSet = 0)
	{
		int livesOnSet = rDiscSet.GetSetNumForWriteFiles(rFilename);
		
		// does the caller want to know which set it's on?
		if(pOnDiscSet) *pOnDiscSet = livesOnSet;
		
		// Make the string
		std::string r(rDiscSet[livesOnSet]);
		r += DIRECTORY_SEPARATOR_ASCHAR;
		r += rFilename;
		r += RAIDFILE_WRITE_EXTENSION;
		return r;
	}
};

#endif // RAIDFILEUTIL__H

