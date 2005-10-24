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

