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
//		Name:    RaidFileController.cpp
//		Purpose: Controls config and daemon comms for RaidFile classes
//		Created: 2003/07/08
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <stdio.h>

#include "RaidFileController.h"
#include "RaidFileException.h"
#include "Configuration.h"

#include "MemLeakFindOn.h"

RaidFileController RaidFileController::mController;

// --------------------------------------------------------------------------
//
// Function
//		Name:    RaidFileController::RaidFileController()
//		Purpose: Constructor
//		Created: 2003/07/08
//
// --------------------------------------------------------------------------
RaidFileController::RaidFileController()
{
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    RaidFileController::~RaidFileController()
//		Purpose: Destructor
//		Created: 2003/07/08
//
// --------------------------------------------------------------------------
RaidFileController::~RaidFileController()
{
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    RaidFileController::RaidFileController()
//		Purpose: Copy constructor
//		Created: 2003/07/08
//
// --------------------------------------------------------------------------
RaidFileController::RaidFileController(const RaidFileController &rController)
{
	THROW_EXCEPTION(RaidFileException, Internal)
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    RaidFileController::Initialise(const char *)
//		Purpose: Initialises the system, loading the configuration file.
//		Created: 2003/07/08
//
// --------------------------------------------------------------------------
void RaidFileController::Initialise(const char *ConfigFilename)
{
	static const ConfigurationVerifyKey verifykeys[] =
	{
		{"SetNumber",	0,	ConfigTest_Exists | ConfigTest_IsInt, 0},
		{"BlockSize",	0,	ConfigTest_Exists | ConfigTest_IsInt, 0},
		{"Dir0", 		0,	ConfigTest_Exists, 0},
		{"Dir1", 		0,	ConfigTest_Exists, 0},
		{"Dir2", 		0,	ConfigTest_Exists | ConfigTest_LastEntry, 0}
	};
	
	static const ConfigurationVerify subverify = 
	{
		"*",
		0,
		verifykeys,
		ConfigTest_LastEntry,
		0
	};
	
	static const ConfigurationVerify verify = 
	{
		"RAID FILE CONFIG",
		&subverify,
		0,
		ConfigTest_LastEntry,
		0
	};
	
	// Load the configuration
	std::string err;
	std::auto_ptr<Configuration> pconfig = Configuration::LoadAndVerify(ConfigFilename, &verify, err);
	
	if(pconfig.get() == 0 || !err.empty())
	{
		fprintf(stderr, "RaidFile configuation file errors:\n%s", err.c_str());
		THROW_EXCEPTION(RaidFileException, BadConfigFile)
	}
	
	// Use the values
	int expectedSetNum = 0;
	std::vector<std::string> confdiscs(pconfig->GetSubConfigurationNames());
	for(std::vector<std::string>::const_iterator i(confdiscs.begin()); i != confdiscs.end(); ++i)
	{
		const Configuration &disc(pconfig->GetSubConfiguration((*i).c_str()));
		
		int setNum = disc.GetKeyValueInt("SetNumber");
		if(setNum != expectedSetNum)
		{
			THROW_EXCEPTION(RaidFileException, BadConfigFile)			
		}
		RaidFileDiscSet set(setNum, (unsigned int)disc.GetKeyValueInt("BlockSize"));
		// Get the values of the directory keys
		std::string d0(disc.GetKeyValue("Dir0"));
		std::string d1(disc.GetKeyValue("Dir1"));
		std::string d2(disc.GetKeyValue("Dir2"));
		// Are they all different (using RAID) or all the same (not using RAID)
		if(d0 != d1 && d1 != d2 && d0 != d2)
		{
			set.push_back(d0);
			set.push_back(d1);
			set.push_back(d2);
		}
		else if(d0 == d1 && d0 == d2)
		{
			// Just push the first one, which is the non-RAID place to store files
			set.push_back(d0);
		}
		else
		{
			// One must be the same as another! Which is bad.
			THROW_EXCEPTION(RaidFileException, BadConfigFile)			
		}
		mSetList.push_back(set);
		expectedSetNum++;
	}
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    RaidFileController::GetDiscSet(int)
//		Purpose: Returns the numbered disc set
//		Created: 2003/07/08
//
// --------------------------------------------------------------------------
RaidFileDiscSet &RaidFileController::GetDiscSet(unsigned int DiscSetNum)
{
	if(DiscSetNum < 0 || DiscSetNum >= mSetList.size())
	{
		THROW_EXCEPTION(RaidFileException, NoSuchDiscSet)
	}

	return mSetList[DiscSetNum];
}



// --------------------------------------------------------------------------
//
// Function
//		Name:    RaidFileDiscSet::GetSetNumForWriteFiles(const std::string &)
//		Purpose: Returns the set number the 'temporary' written files should
//				 be stored on, given a filename.
//		Created: 2003/07/10
//
// --------------------------------------------------------------------------
int RaidFileDiscSet::GetSetNumForWriteFiles(const std::string &rFilename) const
{
	// Simple hash function, add up the ASCII values of all the characters,
	// and get modulo number of partitions in the set.
	std::string::const_iterator i(rFilename.begin());
	int h = 0;
	for(; i != rFilename.end(); ++i)
	{
		h += (*i);
	}
	return h % size();
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    RaidFileController::DiscSetPathToFileSystemPath(unsigned int, const std::string &, int)
//		Purpose: Given a Raid File style file name, return a filename for the physical filing system.
//				 DiscOffset is effectively the disc number (but remember files are rotated around the
//				 discs in a disc set)
//		Created: 19/1/04
//
// --------------------------------------------------------------------------
std::string RaidFileController::DiscSetPathToFileSystemPath(unsigned int DiscSetNum, const std::string &rFilename, int DiscOffset)
{
	if(DiscSetNum < 0 || DiscSetNum >= mController.mSetList.size())
	{
		THROW_EXCEPTION(RaidFileException, NoSuchDiscSet)
	}

	// Work out which disc it's to be on
	int disc = (mController.mSetList[DiscSetNum].GetSetNumForWriteFiles(rFilename) + DiscOffset)
						% mController.mSetList[DiscSetNum].size();
	
	// Make the string
	std::string r((mController.mSetList[DiscSetNum])[disc]);
	r += DIRECTORY_SEPARATOR_ASCHAR;
	r += rFilename;
	return r;
}



