// --------------------------------------------------------------------------
//
// File
//		Name:    BackupStoreObjectDump.cpp
//		Purpose: Implementations of dumping objects to stdout/TRACE
//		Created: 3/5/04
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <stdarg.h>
#include <stdio.h>

#include <map>
#include <string>

#include "autogen_BackupStoreException.h"
#include "BackupStoreDirectory.h"
#include "BackupStoreFile.h"
#include "BackupStoreFileWire.h"
#include "BackupStoreFilename.h"
#include "BackupClientFileAttributes.h"
#include "BackupStoreObjectMagic.h"
#include "Exception.h"

#include "MemLeakFindOn.h"


// --------------------------------------------------------------------------
//
// Function
//		Name:    static void OutputLine(FILE *, bool, const char *, ...)
//		Purpose: Output a line for the object dumping, to file and/or trace...
//		Created: 3/5/04
//
// --------------------------------------------------------------------------
static void OutputLine(std::ostream* pOutput, bool ToTrace, const char *format, ...)
{
	char text[512];
	int r = 0;
	va_list ap;
	va_start(ap, format);
	r = vsnprintf(text, sizeof(text), format, ap);
	va_end(ap);

	if(pOutput != 0)
	{
		(*pOutput) << text << "\n";
	}

	if(ToTrace)
	{
		BOX_TRACE(text);
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
void BackupStoreDirectory::Dump(std::ostream& output, bool ToTrace)
{
	OutputLine(&output, ToTrace, "Directory object.\nObject ID: %llx\nContainer ID: %llx\nNumber entries: %d\n"\
		"Attributes mod time: %llx\nAttributes size: %d\n", mObjectID, mContainerID, mEntries.size(),
		mAttributesModTime, mAttributes.GetSize());

	// So repeated filenames can be illustrated, even though they can't be decoded
	std::map<std::string, int> nameNum;
	int nameNumI = 0;

	// Dump items
	OutputLine(&output, ToTrace, "Items:");
	OutputLine(&output, ToTrace, "ID     Size AttrHash         AtSz NSz NIdx Flags");
	OutputLine(&output, ToTrace, "====== ==== ================ ==== === ==== =====");
	for(std::vector<Entry*>::const_iterator i(mEntries.begin()); i != mEntries.end(); ++i)
	{
		// Choose file name index number for this file
		std::map<std::string, int>::iterator nn(nameNum.find((*i)->GetName().GetEncodedFilename()));
		int ni = nameNumI;
		if(nn != nameNum.end())
		{
			ni = nn->second;
		}
		else
		{
			nameNum[(*i)->GetName().GetEncodedFilename()] = nameNumI;
			++nameNumI;
		}
		
		// Do dependencies
		char depends[128];
		depends[0] = '\0';
		int depends_l = 0;
		if((*i)->GetDependsOnObject() != 0)
		{
#ifdef _MSC_VER
			depends_l += ::sprintf(depends + depends_l, " depOn(%I64x)", (*i)->GetDependsOnObject());
#else
			depends_l += ::sprintf(depends + depends_l, " depOn(%llx)", (long long)((*i)->GetDependsOnObject()));
#endif
		}
		if((*i)->GetRequiredByObject() != 0)
		{
#ifdef _MSC_VER
			depends_l += ::sprintf(depends + depends_l, " reqBy(%I64x)", (*i)->GetRequiredByObject());
#else
			depends_l += ::sprintf(depends + depends_l, " reqBy(%llx)", (long long)((*i)->GetRequiredByObject()));
#endif
		}

		// Output item
		int16_t f = (*i)->GetFlags();
#ifdef WIN32
		OutputLine(&output, ToTrace,
			"%06I64x %4I64d %016I64x %4d %3d %4d%s%s%s%s%s%s",
#else
		OutputLine(&output, ToTrace,
			"%06llx %4lld %016llx %4d %3d %4d%s%s%s%s%s%s",
#endif
			(*i)->GetObjectID(),
			(*i)->GetSizeInBlocks(),
			(*i)->GetAttributesHash(),
			(*i)->GetAttributes().GetSize(),
			(*i)->GetName().GetEncodedFilename().size(),
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
void BackupStoreFile::DumpFile(std::ostream& output, bool ToTrace, IOStream &rFile)
{
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
		OutputLine(&output, ToTrace, "File header doesn't have the correct magic, aborting dump");
		return;
	}

	OutputLine(&output, ToTrace, "File object.\nContainer ID: %llx\nModification time: %llx\n"\
		"Max block clear size: %d\nOptions: %08x\nNum blocks: %d", box_ntoh64(hdr.mContainerID),
			box_ntoh64(hdr.mModificationTime), ntohl(hdr.mMaxBlockClearSize), ntohl(hdr.mOptions),
			box_ntoh64(hdr.mNumBlocks));

	// Read the next two objects
	BackupStoreFilename fn;
	fn.ReadFromStream(rFile, IOStream::TimeOutInfinite);
	OutputLine(&output, ToTrace, "Filename size: %d", fn.GetEncodedFilename().size());
	
	BackupClientFileAttributes attr;
	attr.ReadFromStream(rFile, IOStream::TimeOutInfinite);
	OutputLine(&output, ToTrace, "Attributes size: %d", attr.GetSize());
	
	// Dump the blocks
	rFile.Seek(0, IOStream::SeekType_Absolute);
	BackupStoreFile::MoveStreamPositionToBlockIndex(rFile);

	// Read in header
	file_BlockIndexHeader bhdr;
	rFile.ReadFullBuffer(&bhdr, sizeof(bhdr), 0);
	if(bhdr.mMagicValue != (int32_t)htonl(OBJECTMAGIC_FILE_BLOCKS_MAGIC_VALUE_V1)
		&& bhdr.mMagicValue != (int32_t)htonl(OBJECTMAGIC_FILE_BLOCKS_MAGIC_VALUE_V0))
	{
		OutputLine(&output, ToTrace, "WARNING: Block header doesn't have the correct magic");
	}
	// number of blocks
	int64_t nblocks = box_ntoh64(bhdr.mNumBlocks);
	OutputLine(&output, ToTrace, "Other file ID (for block refs): %llx\nNum blocks (in blk hdr): %lld",
		box_ntoh64(bhdr.mOtherFileID), nblocks);

	// Dump info about each block
	OutputLine(&output, ToTrace, "Index    Where EncSz/Idx");
	OutputLine(&output, ToTrace, "======== ===== ==========");
	int64_t nnew = 0, nold = 0;
	for(int64_t b = 0; b < nblocks; ++b)
	{
		file_BlockIndexEntry en;
		if(!rFile.ReadFullBuffer(&en, sizeof(en), 0))
		{
			OutputLine(&output, ToTrace, "Didn't manage to read block %lld from file\n", b);
			continue;
		}
		int64_t s = box_ntoh64(en.mEncodedSize);
		if(s > 0)
		{
			nnew++;
			output << std::setw(8) << b << " this  s=" << std::setw(8) << s << "\n";
			if(ToTrace)
			{
				BOX_TRACE(std::setw(8) << b << " this  s=" << std::setw(8) << s);
			}
		}
		else
		{
			nold++;
			output << std::setw(8) << b << " other i=" << std::setw(8) << 0 - s << "\n";
			if(ToTrace)
			{
				BOX_TRACE(std::setw(8) << b << " other i=" << std::setw(8) << 0 - s);
			}
		}
	}
	OutputLine(&output, ToTrace, "======== ===== ==========");
}

