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
//		Name:    RaidFileController.h
//		Purpose: Controls config and daemon comms for RaidFile classes
//		Created: 2003/07/08
//
// --------------------------------------------------------------------------

/*  NOTE: will log to local5: include a line like
	local5.info                                             /var/log/raidfile
	in /etc/syslog.conf
*/

#ifndef RAIDFILECONTROLLER__H
#define RAIDFILECONTROLLER__H

#include <string>
#include <vector>

// --------------------------------------------------------------------------
//
// Class
//		Name:    RaidFileDiscSet
//		Purpose: Describes a set of paritions for RAID like files.
//				 Use as list of directories containing the files.
//		Created: 2003/07/08
//
// --------------------------------------------------------------------------
class RaidFileDiscSet : public std::vector<std::string>
{
public:
	RaidFileDiscSet(int SetID, unsigned int BlockSize)
		: mSetID(SetID),
		  mBlockSize(BlockSize)
	{
	}
	RaidFileDiscSet(const RaidFileDiscSet &rToCopy)
		: std::vector<std::string>(rToCopy),
		  mSetID(rToCopy.mSetID),
		  mBlockSize(rToCopy.mBlockSize)
	{
	}
	
	~RaidFileDiscSet()
	{
	}

	int GetSetID() const {return mSetID;}
	
	int GetSetNumForWriteFiles(const std::string &rFilename) const;
	
	unsigned int GetBlockSize() const {return mBlockSize;}

	// Is this disc set a non-RAID disc set? (ie files never get transformed to raid storage)
	bool IsNonRaidSet() const {return 1 == size();}

private:
	int mSetID;
	unsigned int mBlockSize;
};

class _RaidFileController;	// compiler warning avoidance

// --------------------------------------------------------------------------
//
// Class
//		Name:    RaidFileController
//		Purpose: Manages the configuration of the RaidFile system, handles
//				 communication with the daemon.
//		Created: 2003/07/08
//
// --------------------------------------------------------------------------
class RaidFileController
{
	friend class _RaidFileController;	// to avoid compiler warning
private:
	RaidFileController();
	RaidFileController(const RaidFileController &rController);
public:
	~RaidFileController();
	
public:
	void Initialise(const char *ConfigFilename = "/etc/box/raidfile.conf");
	int GetNumDiscSets() {return mSetList.size();}

	// --------------------------------------------------------------------------
	//
	// Function
	//		Name:    RaidFileController::GetController()
	//		Purpose: Gets the one and only controller object.
	//		Created: 2003/07/08
	//
	// --------------------------------------------------------------------------	
	static RaidFileController &GetController() {return mController;}
	RaidFileDiscSet &GetDiscSet(unsigned int DiscSetNum);

	static std::string DiscSetPathToFileSystemPath(unsigned int DiscSetNum, const std::string &rFilename, int DiscOffset);
	
private:
	std::vector<RaidFileDiscSet> mSetList;
	
	static RaidFileController mController;
};

#endif // RAIDFILECONTROLLER__H

