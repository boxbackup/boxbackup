// --------------------------------------------------------------------------
//
// File
//		Name:    bbackupobjdump.cpp
//		Purpose: Dump contents of backup objects
//		Created: 3/5/04
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <cstdio>
#include <cstring>

#include "MainHelper.h"
#include "FileStream.h"
#include "BackupStoreDirectory.h"
#include "BackupStoreFile.h"
#include "BackupStoreObjectMagic.h"

#include "MemLeakFindOn.h"

// --------------------------------------------------------------------------
//
// Function
//		Name:    int main(int, const char *[])
//		Purpose: Main fn for bbackupobjdump
//		Created: 3/5/04
//
// --------------------------------------------------------------------------
int main(int argc, const char *argv[])
{
	MAINHELPER_START

	if(argc != 2)
	{
		::printf("Input file not specified.\nUsage: bbackupobjdump <input file>\n");
		return 1;
	}

	// Open file
	FileStream file(argv[1]);
	
	// Read magic number
	uint32_t signature;
	if(file.Read(&signature, sizeof(signature)) != sizeof(signature))
	{
		// Too short, can't read signature from it
		return false;
	}
	// Seek back to beginning
	file.Seek(0, IOStream::SeekType_Absolute);
	
	// Then... check depending on the type
	switch(ntohl(signature))
	{
	case OBJECTMAGIC_FILE_MAGIC_VALUE_V1:
#ifndef BOX_DISABLE_BACKWARDS_COMPATIBILITY_BACKUPSTOREFILE
	case OBJECTMAGIC_FILE_MAGIC_VALUE_V0:
#endif
		BackupStoreFile::DumpFile(stdout, false, file);
		break;

	case OBJECTMAGIC_DIR_MAGIC_VALUE:
		{
			BackupStoreDirectory dir;
			dir.ReadFromStream(file, IOStream::TimeOutInfinite);
			dir.Dump(stdout, false);
			if(dir.CheckAndFix())
			{
				::printf("Directory didn't pass checking\n");
			}
		}
		break;

	default:
		::printf("File does not appear to be a valid box backup object.\n");
		break;
	}

	MAINHELPER_END
}

