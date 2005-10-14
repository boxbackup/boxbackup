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

