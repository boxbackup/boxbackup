// --------------------------------------------------------------------------
//
// File
//		Name:    BackupStoreObjectDump.cpp
//		Purpose: Implementations of dumping objects to stdout/TRACE
//		Created: 3/5/04
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <stdio.h>
#include <stdarg.h>
#include <map>

#include "BackupStoreDirectory.h"
#include "BackupStoreFile.h"
#include "BackupStoreFileWire.h"
#include "autogen_BackupStoreException.h"
#include "BackupStoreFilename.h"
#include "BackupClientFileAttributes.h"
#include "BackupStoreObjectMagic.h"

#include "MemLeakFindOn.h"


// --------------------------------------------------------------------------
//
// Function
//		Name:    static void OutputLine(FILE *, bool, const char *, ...)
//		Purpose: Output a line for the object dumping, to file and/or trace...
//		Created: 3/5/04
//
// --------------------------------------------------------------------------
static void OutputLine(FILE *file, bool ToTrace, const char *format, ...)
{
	char text[512];
	int r = 0;
	va_list ap;
	va_start(ap, format);
	r = vsnprintf(text, sizeof(text), format, ap);
	va_end(ap);

	if(file != 0)
	{
		::fprintf(file, "%s", text);		
	}
	if(ToTrace)
	{
		TRACE1("%s", text);
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreDirectory::Dump(void *clibFileHandle, bool ToTrace)
//		Purpose: (first arg is FILE *, but avoid including stdio.h everywhere)
//				 Dump the contents to a file, or trace.
//		Created: 3/5/04
//
// --------------------------------------------------------------------------
void BackupStoreDirectory::Dump(void *clibFileHandle, bool ToTrace)
{
	FILE *file = (FILE*)clibFileHandle;

	OutputLine(file, ToTrace, "Directory object.\nObject ID: %llx\nContainer ID: %llx\nNumber entries: %d\n"\
		"Attributes mod time: %llx\nAttributes size: %d\n", mObjectID, mContainerID, mEntries.size(),
		mAttributesModTime, mAttributes.GetSize());

	// So repeated filenames can be illustrated, even though they can't be decoded
	std::map<BackupStoreFilename, int> nameNum;
	int nameNumI = 0;

	// Dump items
	OutputLine(file, ToTrace, "Items:\nID     Size AttrHash         AtSz NSz NIdx Flags\n");
	for(std::vector<Entry*>::const_iterator i(mEntries.begin()); i != mEntries.end(); ++i)
	{
		// Choose file name index number for this file
		std::map<BackupStoreFilename, int>::iterator nn(nameNum.find((*i)->GetName()));
		int ni = nameNumI;
		if(nn != nameNum.end())
		{
			ni = nn->second;
		}
		else
		{
			nameNum[(*i)->GetName()] = nameNumI;
			++nameNumI;
		}
		
		// Do dependencies
		char depends[128];
		depends[0] = '\0';
		int depends_l = 0;
		if((*i)->GetDependsNewer() != 0)
		{
			depends_l += ::sprintf(depends + depends_l, " depNew(%llx)", (*i)->GetDependsNewer());
		}
		if((*i)->GetDependsOlder() != 0)
		{
			depends_l += ::sprintf(depends + depends_l, " depOld(%llx)", (*i)->GetDependsOlder());
		}

		// Output item
		int16_t f = (*i)->GetFlags();
		OutputLine(file, ToTrace, "%06llx %4lld %016llx %4d %3d %4d%s%s%s%s%s%s\n",
			(*i)->GetObjectID(),
			(*i)->GetSizeInBlocks(),
			(*i)->GetAttributesHash(),
			(*i)->GetAttributes().GetSize(),
			(*i)->GetName().size(),
			ni,
			((f & BackupStoreDirectory::Entry::Flags_File)?" file":""),
			((f & BackupStoreDirectory::Entry::Flags_Dir)?" dir":""),
			((f & BackupStoreDirectory::Entry::Flags_Deleted)?" del":""),
			((f & BackupStoreDirectory::Entry::Flags_OldVersion)?" old":""),
			((f & BackupStoreDirectory::Entry::Flags_RemoveASAP)?" removeASAP":""),
			depends);
	}
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreFile::DumpFile(void *, bool, IOStream &)
//		Purpose: (first arg is FILE *, but avoid including stdio.h everywhere)
//				 Dump the contents to a file, or trace.
//		Created: 4/5/04
//
// --------------------------------------------------------------------------
void BackupStoreFile::DumpFile(void *clibFileHandle, bool ToTrace, IOStream &rFile)
{
	FILE *file = (FILE*)clibFileHandle;

	// Read header
	file_StreamFormat hdr;
	if(!rFile.ReadFullBuffer(&hdr, sizeof(hdr),
		0 /* not interested in bytes read if this fails */, IOStream::TimeOutInfinite))
	{
		// Couldn't read header
		THROW_EXCEPTION(BackupStoreException, WhenDecodingExpectedToReadButCouldnt)
	}

	// Check and output header info
	if(hdr.mMagicValue != (int32_t)htonl(OBJECTMAGIC_FILE_MAGIC_VALUE_V1)
		&& hdr.mMagicValue != (int32_t)htonl(OBJECTMAGIC_FILE_MAGIC_VALUE_V0))
	{
		OutputLine(file, ToTrace, "File header doesn't have the correct magic, aborting dump\n");
		return;
	}

	OutputLine(file, ToTrace, "File object.\nContainer ID: %llx\nModification time: %llx\n"\
		"Max block clear size: %d\nOptions: %08x\nNum blocks: %d\n", box_ntoh64(hdr.mContainerID),
			box_ntoh64(hdr.mModificationTime), ntohl(hdr.mMaxBlockClearSize), ntohl(hdr.mOptions),
			box_ntoh64(hdr.mNumBlocks));

	// Read the next two objects
	BackupStoreFilename fn;
	fn.ReadFromStream(rFile, IOStream::TimeOutInfinite);
	OutputLine(file, ToTrace, "Filename size: %d\n", fn.size());
	
	BackupClientFileAttributes attr;
	attr.ReadFromStream(rFile, IOStream::TimeOutInfinite);
	OutputLine(file, ToTrace, "Attributes size: %d\n", attr.GetSize());
	
	// Dump the blocks
	rFile.Seek(0, IOStream::SeekType_Absolute);
	BackupStoreFile::MoveStreamPositionToBlockIndex(rFile);

	// Read in header
	file_BlockIndexHeader bhdr;
	rFile.ReadFullBuffer(&bhdr, sizeof(bhdr), 0);
	if(bhdr.mMagicValue != (int32_t)htonl(OBJECTMAGIC_FILE_BLOCKS_MAGIC_VALUE_V1)
		&& bhdr.mMagicValue != (int32_t)htonl(OBJECTMAGIC_FILE_BLOCKS_MAGIC_VALUE_V0))
	{
		OutputLine(file, ToTrace, "WARNING: Block header doesn't have the correct magic\n");
	}
	// number of blocks
	int64_t nblocks = box_ntoh64(bhdr.mNumBlocks);
	OutputLine(file, ToTrace, "Other file ID (for block refs): %llx\nNum blocks (in blk hdr): %lld\n",
		box_ntoh64(bhdr.mOtherFileID), nblocks);

	// Dump info about each block
	OutputLine(file, ToTrace, "======== ===== ==========\n   Index Where  EncSz/Idx\n");
	int64_t nnew = 0, nold = 0;
	for(int64_t b = 0; b < nblocks; ++b)
	{
		file_BlockIndexEntry en;
		if(!rFile.ReadFullBuffer(&en, sizeof(en), 0))
		{
			OutputLine(file, ToTrace, "Didn't manage to read block %lld from file\n", b);
			continue;
		}
		int64_t s = box_ntoh64(en.mEncodedSize);
		if(s > 0)
		{
			nnew++;
			TRACE2("%8lld this  s=%8lld\n", b, s);
		}
		else
		{
			nold++;
			TRACE2("%8lld other i=%8lld\n", b, 0 - s);		
		}
	}
	TRACE0("======== ===== ==========\n");
}

