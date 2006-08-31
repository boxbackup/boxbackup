// --------------------------------------------------------------------------
//
// File
//		Name:    BackupStoreFile.cpp
//		Purpose: Utils for manipulating files
//		Created: 2003/08/28
//
// --------------------------------------------------------------------------

#include "Box.h"

#ifdef HAVE_UNISTD_H
	#include <unistd.h>
#endif

#include <sys/stat.h>
#include <string.h>
#include <new>
#include <string.h>
#ifndef BOX_DISABLE_BACKWARDS_COMPATIBILITY_BACKUPSTOREFILE
	#ifndef WIN32
		#include <syslog.h>
	#endif
	#include <stdio.h>
#endif

#include "BackupStoreFile.h"
#include "BackupStoreFileWire.h"
#include "BackupStoreFileCryptVar.h"
#include "BackupStoreFilename.h"
#include "BackupStoreException.h"
#include "IOStream.h"
#include "Guards.h"
#include "FileModificationTime.h"
#include "FileStream.h"
#include "BackupClientFileAttributes.h"
#include "BackupStoreObjectMagic.h"
#include "Compress.h"
#include "CipherContext.h"
#include "CipherBlowfish.h"
#include "CipherAES.h"
#include "BackupStoreConstants.h"
#include "CollectInBufferStream.h"
#include "RollingChecksum.h"
#include "MD5Digest.h"
#include "ReadGatherStream.h"
#include "Random.h"
#include "BackupStoreFileEncodeStream.h"

#include "MemLeakFindOn.h"

using namespace BackupStoreFileCryptVar;

// How big a buffer to use for copying files
#define COPY_BUFFER_SIZE	(8*1024)

// Statistics
BackupStoreFileStats BackupStoreFile::msStats = {0,0,0};

#ifndef BOX_DISABLE_BACKWARDS_COMPATIBILITY_BACKUPSTOREFILE
	bool sWarnedAboutBackwardsCompatiblity = false;
#endif

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreFile::EncodeFile(IOStream &, IOStream &)
//		Purpose: Encode a file into something for storing on file server.
//				 Requires a real filename so full info can be stored.
//
//				 Returns a stream. Most of the work is done by the stream
//				 when data is actually requested -- the file will be held
//				 open until the stream is deleted or the file finished.
//		Created: 2003/08/28
//
// --------------------------------------------------------------------------
std::auto_ptr<IOStream> BackupStoreFile::EncodeFile(const char *Filename, int64_t ContainerID, const BackupStoreFilename &rStoreFilename, int64_t *pModificationTime)
{
	// Create the stream
	std::auto_ptr<IOStream> stream(new BackupStoreFileEncodeStream);

	// Do the initial setup
	((BackupStoreFileEncodeStream*)stream.get())->Setup(Filename, 0 /* no recipe, just encode */,
		ContainerID, rStoreFilename, pModificationTime);
	
	// Return the stream for the caller
	return stream;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreFile::VerifyEncodedFileFormat(IOStream &)
//		Purpose: Verify that an encoded file meets the format requirements.
//				 Doesn't verify that the data is intact and can be decoded.
//				 Optionally returns the ID of the file which it is diffed from,
//				 and the (original) container ID.
//		Created: 2003/08/28
//
// --------------------------------------------------------------------------
bool BackupStoreFile::VerifyEncodedFileFormat(IOStream &rFile, int64_t *pDiffFromObjectIDOut, int64_t *pContainerIDOut)
{
	// Get the size of the file
	int64_t fileSize = rFile.BytesLeftToRead();
	if(fileSize == IOStream::SizeOfStreamUnknown)
	{
		THROW_EXCEPTION(BackupStoreException, StreamDoesntHaveRequiredFeatures)
	}

	// Get the header...
	file_StreamFormat hdr;
	if(!rFile.ReadFullBuffer(&hdr, sizeof(hdr), 0 /* not interested in bytes read if this fails */))
	{
		// Couldn't read header
		return false;
	}
	
	// Check magic number
	if(ntohl(hdr.mMagicValue) != OBJECTMAGIC_FILE_MAGIC_VALUE_V1
#ifndef BOX_DISABLE_BACKWARDS_COMPATIBILITY_BACKUPSTOREFILE
		&& ntohl(hdr.mMagicValue) != OBJECTMAGIC_FILE_MAGIC_VALUE_V0
#endif
		)
	{
		return false;
	}
	
	// Get a filename, see if it loads OK
	try
	{
		BackupStoreFilename fn;
		fn.ReadFromStream(rFile, IOStream::TimeOutInfinite);
	}
	catch(...)
	{
		// an error occured while reading it, so that's not good
		return false;
	}
	
	// Skip the attributes -- because they're encrypted, the server can't tell whether they're OK or not
	try
	{
		int32_t size_s;
		if(!rFile.ReadFullBuffer(&size_s, sizeof(size_s), 0 /* not interested in bytes read if this fails */))
		{
			THROW_EXCEPTION(CommonException, StreamableMemBlockIncompleteRead)
		}
		int size = ntohl(size_s);
		// Skip forward the size
		rFile.Seek(size, IOStream::SeekType_Relative);
	}
	catch(...)
	{
		// an error occured while reading it, so that's not good
		return false;
	}

	// Get current position in file -- the end of the header
	int64_t headerEnd = rFile.GetPosition();
	
	// Get number of blocks
	int64_t numBlocks = box_ntoh64(hdr.mNumBlocks);
	
	// Calculate where the block index will be, check it's reasonable
	int64_t blockIndexLoc = fileSize - ((numBlocks * sizeof(file_BlockIndexEntry)) + sizeof(file_BlockIndexHeader));
	if(blockIndexLoc < headerEnd)
	{
		// Not enough space left for the block index, let alone the blocks themselves
		return false;
	}

	// Load the block index header
	rFile.Seek(blockIndexLoc, IOStream::SeekType_Absolute);
	file_BlockIndexHeader blkhdr;
	if(!rFile.ReadFullBuffer(&blkhdr, sizeof(blkhdr), 0 /* not interested in bytes read if this fails */))
	{
		// Couldn't read block index header -- assume bad file
		return false;
	}
	
	// Check header
	if((ntohl(blkhdr.mMagicValue) != OBJECTMAGIC_FILE_BLOCKS_MAGIC_VALUE_V1
#ifndef BOX_DISABLE_BACKWARDS_COMPATIBILITY_BACKUPSTOREFILE
		&& ntohl(blkhdr.mMagicValue) != OBJECTMAGIC_FILE_BLOCKS_MAGIC_VALUE_V0
#endif
		)
		|| (int64_t)box_ntoh64(blkhdr.mNumBlocks) != numBlocks)
	{
		// Bad header -- either magic value or number of blocks is wrong
		return false;
	}
	
	// Flag for recording whether a block is referenced from another file
	bool blockFromOtherFileReferenced = false;
	
	// Read the index, checking that the length values all make sense
	int64_t currentBlockStart = headerEnd;
	for(int64_t b = 0; b < numBlocks; ++b)
	{
		// Read block entry
		file_BlockIndexEntry blk;
		if(!rFile.ReadFullBuffer(&blk, sizeof(blk), 0 /* not interested in bytes read if this fails */))
		{
			// Couldn't read block index entry -- assume bad file
			return false;
		}
		
		// Check size and location
		int64_t blkSize = box_ntoh64(blk.mEncodedSize);
		if(blkSize <= 0)
		{
			// Mark that this file references another file
			blockFromOtherFileReferenced = true;
		}
		else
		{
			// This block is actually in this file
			if((currentBlockStart + blkSize) > blockIndexLoc)
			{
				// Encoded size makes the block run over the index
				return false;
			}
			
			// Move the current block start ot the end of this block
			currentBlockStart += blkSize;
		}
	}
	
	// Check that there's no empty space
	if(currentBlockStart != blockIndexLoc)
	{
		return false;
	}
	
	// Check that if another block is references, then the ID is there, and if one isn't there is no ID.
	int64_t otherID = box_ntoh64(blkhdr.mOtherFileID);
	if((otherID != 0 && blockFromOtherFileReferenced == false)
		|| (otherID == 0 && blockFromOtherFileReferenced == true))
	{
		// Doesn't look good!
		return false;
	}
	
	// Does the caller want the other ID?
	if(pDiffFromObjectIDOut)
	{
		*pDiffFromObjectIDOut = otherID;
	}
	
	// Does the caller want the container ID?
	if(pContainerIDOut)
	{
		*pContainerIDOut = box_ntoh64(hdr.mContainerID);
	}

	// Passes all tests
	return true;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreFile::DecodeFile(IOStream &, const char *)
//		Purpose: Decode a file. Will set file attributes. File must not exist.
//		Created: 2003/08/28
//
// --------------------------------------------------------------------------
void BackupStoreFile::DecodeFile(IOStream &rEncodedFile, const char *DecodedFilename, int Timeout, const BackupClientFileAttributes *pAlterativeAttr)
{
	// Does file exist?
	struct stat st;
	if(::stat(DecodedFilename, &st) == 0)
	{
		THROW_EXCEPTION(BackupStoreException, OutputFileAlreadyExists)
	}
	
	// Try, delete output file if error
	try
	{
		// Make a stream for outputting this file
		FileStream out(DecodedFilename, O_WRONLY | O_CREAT | O_EXCL);

		// Get the decoding stream
		std::auto_ptr<DecodedStream> stream(DecodeFileStream(rEncodedFile, Timeout, pAlterativeAttr));
		
		// Is it a symlink?
		if(!stream->IsSymLink())
		{
			// Copy it out to the file
			stream->CopyStreamTo(out);
		}

		out.Close();
		
		// Write the attributes
		stream->GetAttributes().WriteAttributes(DecodedFilename);
	}
	catch(...)
	{
		::unlink(DecodedFilename);
		throw;
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreFile::DecodeFileStream(IOStream &, int, const BackupClientFileAttributes *)
//		Purpose: Return a stream which will decode the encrypted file data on the fly.
//				 Accepts streams in block index first, or main header first, order. In the latter case,
//				 the stream must be Seek()able.
//
//				 Before you use the returned stream, call IsSymLink() -- symlink streams won't allow
//				 you to read any data to enforce correct logic. See BackupStoreFile::DecodeFile() implementation.
//		Created: 9/12/03
//
// --------------------------------------------------------------------------
std::auto_ptr<BackupStoreFile::DecodedStream> BackupStoreFile::DecodeFileStream(IOStream &rEncodedFile, int Timeout, const BackupClientFileAttributes *pAlterativeAttr)
{
	// Create stream
	std::auto_ptr<DecodedStream> stream(new DecodedStream(rEncodedFile, Timeout));
	
	// Get it ready
	stream->Setup(pAlterativeAttr);
	
	// Return to caller
	return stream;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreFile::DecodedStream::DecodedStream(IOStream &, int)
//		Purpose: Constructor
//		Created: 9/12/03
//
// --------------------------------------------------------------------------
BackupStoreFile::DecodedStream::DecodedStream(IOStream &rEncodedFile, int Timeout)
	: mrEncodedFile(rEncodedFile),
	  mTimeout(Timeout),
	  mNumBlocks(0),
	  mpBlockIndex(0),
	  mpEncodedData(0),
	  mpClearData(0),
	  mClearDataSize(0),
	  mCurrentBlock(-1),
	  mCurrentBlockClearSize(0),
	  mPositionInCurrentBlock(0),
	  mEntryIVBase(42)	// different to default value in the encoded stream!
#ifndef BOX_DISABLE_BACKWARDS_COMPATIBILITY_BACKUPSTOREFILE
	  , mIsOldVersion(false)
#endif
{
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreFile::DecodedStream::~DecodedStream()
//		Purpose: Desctructor
//		Created: 9/12/03
//
// --------------------------------------------------------------------------
BackupStoreFile::DecodedStream::~DecodedStream()
{
	// Free any allocated memory
	if(mpBlockIndex)
	{
		::free(mpBlockIndex);
	}
	if(mpEncodedData)
	{
		BackupStoreFile::CodingChunkFree(mpEncodedData);
	}
	if(mpClearData)
	{
		::free(mpClearData);
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreFile::DecodedStream::Setup(const BackupClientFileAttributes *)
//		Purpose: Get the stream ready to decode -- reads in headers
//		Created: 9/12/03
//
// --------------------------------------------------------------------------
void BackupStoreFile::DecodedStream::Setup(const BackupClientFileAttributes *pAlterativeAttr)
{
	// Get the size of the file
	int64_t fileSize = mrEncodedFile.BytesLeftToRead();

	// Get the magic number to work out which order the stream is in
	int32_t magic;
	if(!mrEncodedFile.ReadFullBuffer(&magic, sizeof(magic), 0 /* not interested in bytes read if this fails */, mTimeout))
	{
		// Couldn't read magic value
		THROW_EXCEPTION(BackupStoreException, WhenDecodingExpectedToReadButCouldnt)
	}

	bool inFileOrder = true;	
	switch(ntohl(magic))
	{
#ifndef BOX_DISABLE_BACKWARDS_COMPATIBILITY_BACKUPSTOREFILE
	case OBJECTMAGIC_FILE_MAGIC_VALUE_V0:
		mIsOldVersion = true;
		// control flows on
#endif
	case OBJECTMAGIC_FILE_MAGIC_VALUE_V1:
		inFileOrder = true;
		break;

#ifndef BOX_DISABLE_BACKWARDS_COMPATIBILITY_BACKUPSTOREFILE
	case OBJECTMAGIC_FILE_BLOCKS_MAGIC_VALUE_V0:
		mIsOldVersion = true;
		// control flows on
#endif
	case OBJECTMAGIC_FILE_BLOCKS_MAGIC_VALUE_V1:
		inFileOrder = false;
		break;

	default:
		THROW_EXCEPTION(BackupStoreException, BadBackupStoreFile)
	}
	
	// If not in file order, then the index list must be read now
	if(!inFileOrder)
	{
		ReadBlockIndex(true /* have already read and verified the magic number */);
	}

	// Get header
	file_StreamFormat hdr;
	if(inFileOrder)
	{
		// Read the header, without the magic number
		if(!mrEncodedFile.ReadFullBuffer(((uint8_t*)&hdr) + sizeof(magic), sizeof(hdr) - sizeof(magic),
			0 /* not interested in bytes read if this fails */, mTimeout))
		{
			// Couldn't read header
			THROW_EXCEPTION(BackupStoreException, WhenDecodingExpectedToReadButCouldnt)
		}
		// Put in magic number
		hdr.mMagicValue = magic;
	}
	else
	{
		// Not in file order, so need to read the full header
		if(!mrEncodedFile.ReadFullBuffer(&hdr, sizeof(hdr), 0 /* not interested in bytes read if this fails */, mTimeout))
		{
			// Couldn't read header
			THROW_EXCEPTION(BackupStoreException, WhenDecodingExpectedToReadButCouldnt)
		}
	}	

	// Check magic number
	if(ntohl(hdr.mMagicValue) != OBJECTMAGIC_FILE_MAGIC_VALUE_V1
#ifndef BOX_DISABLE_BACKWARDS_COMPATIBILITY_BACKUPSTOREFILE
		&& ntohl(hdr.mMagicValue) != OBJECTMAGIC_FILE_MAGIC_VALUE_V0
#endif
		)
	{
		THROW_EXCEPTION(BackupStoreException, BadBackupStoreFile)
	}

	// Get the filename
	mFilename.ReadFromStream(mrEncodedFile, mTimeout);
	
	// Get the attributes (either from stream, or supplied attributes)
	if(pAlterativeAttr != 0)
	{
		// Read dummy attributes
		BackupClientFileAttributes attr;
		attr.ReadFromStream(mrEncodedFile, mTimeout);

		// Set to supplied attributes
		mAttributes = *pAlterativeAttr;
	}
	else
	{
		// Read the attributes from the stream
		mAttributes.ReadFromStream(mrEncodedFile, mTimeout);
	}
	
	// If it is in file order, go and read the file attributes
	// Requires that the stream can seek
	if(inFileOrder)
	{
		// Make sure the file size is known
		if(fileSize == IOStream::SizeOfStreamUnknown)
		{
			THROW_EXCEPTION(BackupStoreException, StreamDoesntHaveRequiredFeatures)
		}
	
		// Store current location (beginning of encoded blocks)
		int64_t endOfHeaderPos = mrEncodedFile.GetPosition();
		
		// Work out where the index is
		int64_t numBlocks = box_ntoh64(hdr.mNumBlocks);
		int64_t blockHeaderPos = fileSize - ((numBlocks * sizeof(file_BlockIndexEntry)) + sizeof(file_BlockIndexHeader));
		
		// Seek to that position
		mrEncodedFile.Seek(blockHeaderPos, IOStream::SeekType_Absolute);
		
		// Read the block index
		ReadBlockIndex(false /* magic number still to be read */);		
		
		// Seek back to the end of header position, ready for reading the chunks
		mrEncodedFile.Seek(endOfHeaderPos, IOStream::SeekType_Absolute);
	}
	
	// Check view of blocks from block header and file header match
	if(mNumBlocks != (int64_t)box_ntoh64(hdr.mNumBlocks))
	{
		THROW_EXCEPTION(BackupStoreException, BadBackupStoreFile)
	}
	
	// Need to allocate some memory for the two blocks for reading encoded data, and clear data
	if(mNumBlocks > 0)
	{
		// Find the maximum encoded data size
		int32_t maxEncodedDataSize = 0;
		const file_BlockIndexEntry *entry = (file_BlockIndexEntry *)mpBlockIndex;
		ASSERT(entry != 0);
		for(int64_t e = 0; e < mNumBlocks; e++)
		{
			// Get the clear and encoded size
			int32_t encodedSize = box_ntoh64(entry[e].mEncodedSize);
			ASSERT(encodedSize > 0);
			
			// Larger?
			if(encodedSize > maxEncodedDataSize) maxEncodedDataSize = encodedSize;
		}
		
		// Allocate those blocks!
		mpEncodedData = (uint8_t*)BackupStoreFile::CodingChunkAlloc(maxEncodedDataSize + 32);

		// Allocate the block for the clear data, using the hint from the header.
		// If this is wrong, things will exception neatly later on, so it can't be used
		// to do anything more than cause an error on downloading.
		mClearDataSize = OutputBufferSizeForKnownOutputSize(ntohl(hdr.mMaxBlockClearSize)) + 32;
		mpClearData = (uint8_t*)::malloc(mClearDataSize);
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreFile::DecodedStream::ReadBlockIndex(bool)
//		Purpose: Read the block index from the stream, and store in internal buffer (minus header)
//		Created: 9/12/03
//
// --------------------------------------------------------------------------
void BackupStoreFile::DecodedStream::ReadBlockIndex(bool MagicAlreadyRead)
{
	// Header
	file_BlockIndexHeader blkhdr;
	
	// Read it in -- way depends on how whether the magic number has already been read
	if(MagicAlreadyRead)
	{
		// Read the header, without the magic number
		if(!mrEncodedFile.ReadFullBuffer(((uint8_t*)&blkhdr) + sizeof(blkhdr.mMagicValue), sizeof(blkhdr) - sizeof(blkhdr.mMagicValue),
			0 /* not interested in bytes read if this fails */, mTimeout))
		{
			// Couldn't read header
			THROW_EXCEPTION(BackupStoreException, WhenDecodingExpectedToReadButCouldnt)
		}
	}
	else
	{
		// Magic not already read, so need to read the full header
		if(!mrEncodedFile.ReadFullBuffer(&blkhdr, sizeof(blkhdr), 0 /* not interested in bytes read if this fails */, mTimeout))
		{
			// Couldn't read header
			THROW_EXCEPTION(BackupStoreException, WhenDecodingExpectedToReadButCouldnt)
		}
		
		// Check magic value
		if(ntohl(blkhdr.mMagicValue) != OBJECTMAGIC_FILE_BLOCKS_MAGIC_VALUE_V1
#ifndef BOX_DISABLE_BACKWARDS_COMPATIBILITY_BACKUPSTOREFILE
			&& ntohl(blkhdr.mMagicValue) != OBJECTMAGIC_FILE_BLOCKS_MAGIC_VALUE_V0
#endif
			)
		{
			THROW_EXCEPTION(BackupStoreException, BadBackupStoreFile)
		}
	}
	
	// Get the number of blocks out of the header
	mNumBlocks = box_ntoh64(blkhdr.mNumBlocks);
	
	// Read the IV base
	mEntryIVBase = box_ntoh64(blkhdr.mEntryIVBase);
	
	// Load the block entries in?
	if(mNumBlocks > 0)
	{
		// How big is the index?
		int64_t indexSize = sizeof(file_BlockIndexEntry) * mNumBlocks;
		
		// Allocate some memory
		mpBlockIndex = ::malloc(indexSize);
		if(mpBlockIndex == 0)
		{
			throw std::bad_alloc();
		}
		
		// Read it in
		if(!mrEncodedFile.ReadFullBuffer(mpBlockIndex, indexSize, 0 /* not interested in bytes read if this fails */, mTimeout))
		{
			// Couldn't read header
			THROW_EXCEPTION(BackupStoreException, WhenDecodingExpectedToReadButCouldnt)
		}
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreFile::DecodedStream::Read(void *, int, int)
//		Purpose: As interface. Reads decrpyted data.
//		Created: 9/12/03
//
// --------------------------------------------------------------------------
int BackupStoreFile::DecodedStream::Read(void *pBuffer, int NBytes, int Timeout)
{
	// Symlinks don't have data. So can't read it. Not even zero bytes.
	if(IsSymLink())
	{
		// Don't allow reading in this case
		THROW_EXCEPTION(BackupStoreException, ThereIsNoDataInASymLink);
	}

	// Already finished?
	if(mCurrentBlock >= mNumBlocks)
	{
		// At end of stream, nothing to do
		return 0;
	}

	int bytesToRead = NBytes;
	uint8_t *output = (uint8_t*)pBuffer;
	
	while(bytesToRead > 0 && mCurrentBlock < mNumBlocks)
	{
		// Anything left in the current block?
		if(mPositionInCurrentBlock < mCurrentBlockClearSize)
		{
			// Copy data out of this buffer
			int s = mCurrentBlockClearSize - mPositionInCurrentBlock;
			if(s > bytesToRead) s = bytesToRead;	// limit to requested data
			
			// Copy
			::memcpy(output, mpClearData + mPositionInCurrentBlock, s);
			
			// Update positions
			output += s;
			mPositionInCurrentBlock += s;
			bytesToRead -= s;
		}
		
		// Need to get some more data?
		if(bytesToRead > 0 && mPositionInCurrentBlock >= mCurrentBlockClearSize)
		{
			// Number of next block
			++mCurrentBlock;
			if(mCurrentBlock >= mNumBlocks)
			{
				// Stop now!
				break;
			}
		
			// Get the size from the block index
			const file_BlockIndexEntry *entry = (file_BlockIndexEntry *)mpBlockIndex;
			int32_t encodedSize = box_ntoh64(entry[mCurrentBlock].mEncodedSize);
			if(encodedSize <= 0)
			{
				// The caller is attempting to decode a file which is the direct result of a diff
				// operation, and so does not contain all the data.
				// It needs to be combined with the previous version first.
				THROW_EXCEPTION(BackupStoreException, CannotDecodeDiffedFilesWithoutCombining)
			}
			
			// Load in next block
			if(!mrEncodedFile.ReadFullBuffer(mpEncodedData, encodedSize, 0 /* not interested in bytes read if this fails */, mTimeout))
			{
				// Couldn't read header
				THROW_EXCEPTION(BackupStoreException, WhenDecodingExpectedToReadButCouldnt)
			}
			
			// Decode the data
			mCurrentBlockClearSize = BackupStoreFile::DecodeChunk(mpEncodedData, encodedSize, mpClearData, mClearDataSize);

			// Calculate IV for this entry
			uint64_t iv = mEntryIVBase;
			iv += mCurrentBlock;
			// Convert to network byte order before encrypting with it, so that restores work on
			// platforms with different endiannesses.
			iv = box_hton64(iv);
			sBlowfishDecryptBlockEntry.SetIV(&iv);
			
			// Decrypt the encrypted section
			file_BlockIndexEntryEnc entryEnc;
			int sectionSize = sBlowfishDecryptBlockEntry.TransformBlock(&entryEnc, sizeof(entryEnc),
					entry[mCurrentBlock].mEnEnc, sizeof(entry[mCurrentBlock].mEnEnc));
			if(sectionSize != sizeof(entryEnc))
			{
				THROW_EXCEPTION(BackupStoreException, BlockEntryEncodingDidntGiveExpectedLength)
			}

			// Make sure this is the right size
			if(mCurrentBlockClearSize != (int32_t)ntohl(entryEnc.mSize))
			{
#ifndef BOX_DISABLE_BACKWARDS_COMPATIBILITY_BACKUPSTOREFILE
				if(!mIsOldVersion)
				{
					THROW_EXCEPTION(BackupStoreException, BadBackupStoreFile)
				}
				// Versions 0.05 and previous of Box Backup didn't properly handle endianess of the
				// IV for the encrypted section. Try again, with the thing the other way round
				iv = box_swap64(iv);
				sBlowfishDecryptBlockEntry.SetIV(&iv);
				int sectionSize = sBlowfishDecryptBlockEntry.TransformBlock(&entryEnc, sizeof(entryEnc),
						entry[mCurrentBlock].mEnEnc, sizeof(entry[mCurrentBlock].mEnEnc));
				if(sectionSize != sizeof(entryEnc))
				{
					THROW_EXCEPTION(BackupStoreException, BlockEntryEncodingDidntGiveExpectedLength)
				}
				if(mCurrentBlockClearSize != (int32_t)ntohl(entryEnc.mSize))
				{
					THROW_EXCEPTION(BackupStoreException, BadBackupStoreFile)
				}
				else
				{
					// Warn and log this issue
					if(!sWarnedAboutBackwardsCompatiblity)
					{
						::printf("WARNING: Decoded one or more files using backwards compatibility mode for block index.\n");
						::syslog(LOG_ERR, "WARNING: Decoded one or more files using backwards compatibility mode for block index.\n");
						sWarnedAboutBackwardsCompatiblity = true;
					}
				}
#else
				THROW_EXCEPTION(BackupStoreException, BadBackupStoreFile)
#endif
			}
			
			// Check the digest
			MD5Digest md5;
			md5.Add(mpClearData, mCurrentBlockClearSize);
			md5.Finish();
			if(!md5.DigestMatches((uint8_t*)entryEnc.mStrongChecksum))
			{
				THROW_EXCEPTION(BackupStoreException, BackupStoreFileFailedIntegrityCheck)
			}
			
			// Set vars to say what's happening
			mPositionInCurrentBlock = 0;
		}
	}
	
	ASSERT(bytesToRead >= 0);
	ASSERT(bytesToRead <= NBytes);

	return NBytes - bytesToRead;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreFile::DecodedStream::IsSymLink()
//		Purpose: Is the unencoded file actually a symlink?
//		Created: 10/12/03
//
// --------------------------------------------------------------------------
bool BackupStoreFile::DecodedStream::IsSymLink()
{
	// First, check in with the attributes
	if(!mAttributes.IsSymLink())
	{
		return false;
	}
	
	// So the attributes think it is a symlink.
	// Consistency check...
	if(mNumBlocks != 0)
	{
		THROW_EXCEPTION(BackupStoreException, BadBackupStoreFile)
	}
	
	return true;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreFile::DecodedStream::Write(const void *, int)
//		Purpose: As interface. Throws exception, as you can't write to this stream.
//		Created: 9/12/03
//
// --------------------------------------------------------------------------
void BackupStoreFile::DecodedStream::Write(const void *pBuffer, int NBytes)
{
	THROW_EXCEPTION(BackupStoreException, CantWriteToDecodedFileStream)
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreFile::DecodedStream::StreamDataLeft()
//		Purpose: As interface. Any data left?
//		Created: 9/12/03
//
// --------------------------------------------------------------------------
bool BackupStoreFile::DecodedStream::StreamDataLeft()
{
	return mCurrentBlock < mNumBlocks;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreFile::DecodedStream::StreamClosed()
//		Purpose: As interface. Always returns true, no writing allowed.
//		Created: 9/12/03
//
// --------------------------------------------------------------------------
bool BackupStoreFile::DecodedStream::StreamClosed()
{
	// Can't write to this stream!
	return true;
}





// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreFile::SetBlowfishKey(const void *, int)
//		Purpose: Static. Sets the key to use for encryption and decryption.
//		Created: 7/12/03
//
// --------------------------------------------------------------------------
void BackupStoreFile::SetBlowfishKeys(const void *pKey, int KeyLength, const void *pBlockEntryKey, int BlockEntryKeyLength)
{
	// IVs set later
	sBlowfishEncrypt.Reset();
	sBlowfishEncrypt.Init(CipherContext::Encrypt, CipherBlowfish(CipherDescription::Mode_CBC, pKey, KeyLength));
	sBlowfishDecrypt.Reset();
	sBlowfishDecrypt.Init(CipherContext::Decrypt, CipherBlowfish(CipherDescription::Mode_CBC, pKey, KeyLength));

	sBlowfishEncryptBlockEntry.Reset();
	sBlowfishEncryptBlockEntry.Init(CipherContext::Encrypt, CipherBlowfish(CipherDescription::Mode_CBC, pBlockEntryKey, BlockEntryKeyLength));
	sBlowfishEncryptBlockEntry.UsePadding(false);
	sBlowfishDecryptBlockEntry.Reset();
	sBlowfishDecryptBlockEntry.Init(CipherContext::Decrypt, CipherBlowfish(CipherDescription::Mode_CBC, pBlockEntryKey, BlockEntryKeyLength));
	sBlowfishDecryptBlockEntry.UsePadding(false);
}


#ifndef HAVE_OLD_SSL
// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreFile::SetAESKey(const void *, int)
//		Purpose: Sets the AES key to use for file data encryption. Will select AES as
//				 the cipher to use when encrypting.
//		Created: 27/4/04
//
// --------------------------------------------------------------------------
void BackupStoreFile::SetAESKey(const void *pKey, int KeyLength)
{
	// Setup context
	sAESEncrypt.Reset();
	sAESEncrypt.Init(CipherContext::Encrypt, CipherAES(CipherDescription::Mode_CBC, pKey, KeyLength));
	sAESDecrypt.Reset();
	sAESDecrypt.Init(CipherContext::Decrypt, CipherAES(CipherDescription::Mode_CBC, pKey, KeyLength));
	
	// Set encryption to use this key, instead of the "default" blowfish key
	spEncrypt = &sAESEncrypt;
	sEncryptCipherType = HEADER_AES_ENCODING;
}
#endif


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreFile::MaxBlockSizeForChunkSize(int)
//		Purpose: The maximum output size of a block, given the chunk size
//		Created: 7/12/03
//
// --------------------------------------------------------------------------
int BackupStoreFile::MaxBlockSizeForChunkSize(int ChunkSize)
{
	// Calculate... the maximum size of output by first the largest it could be after compression,
	// which is encrypted, and has a 1 bytes header and the IV added, plus 1 byte for luck
	// And then on top, add 128 bytes just to make sure. (Belts and braces approach to fixing
	// an problem where a rather non-compressable file didn't fit in a block buffer.)
	return sBlowfishEncrypt.MaxOutSizeForInBufferSize(Compress_MaxSizeForCompressedData(ChunkSize)) + 1 + 1
		+ sBlowfishEncrypt.GetIVLength() + 128;
}



// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreFile::EncodeChunk(const void *, int, BackupStoreFile::EncodingBuffer &)
//		Purpose: Encodes a chunk (encryption, possible compressed beforehand)
//		Created: 8/12/03
//
// --------------------------------------------------------------------------
int BackupStoreFile::EncodeChunk(const void *Chunk, int ChunkSize, BackupStoreFile::EncodingBuffer &rOutput)
{
	ASSERT(spEncrypt != 0);

	// Check there's some space in the output block
	if(rOutput.mBufferSize < 256)
	{
		rOutput.Reallocate(256);
	}
	
	// Check alignment of the block
	ASSERT((((uint32_t)(long)rOutput.mpBuffer) % BACKUPSTOREFILE_CODING_BLOCKSIZE) == BACKUPSTOREFILE_CODING_OFFSET);

	// Want to compress it?
	bool compressChunk = (ChunkSize >= BACKUP_FILE_MIN_COMPRESSED_CHUNK_SIZE);

	// Build header
	uint8_t header = sEncryptCipherType << HEADER_ENCODING_SHIFT;
	if(compressChunk) header |= HEADER_CHUNK_IS_COMPRESSED;

	// Store header
	rOutput.mpBuffer[0] = header;
	int outOffset = 1;

	// Setup cipher, and store the IV
	int ivLen = 0;
	const void *iv = spEncrypt->SetRandomIV(ivLen);
	::memcpy(rOutput.mpBuffer + outOffset, iv, ivLen);
	outOffset += ivLen;
	
	// Start encryption process
	spEncrypt->Begin();
	
	#define ENCODECHUNK_CHECK_SPACE(ToEncryptSize)									\
		{																			\
			if((rOutput.mBufferSize - outOffset) < ((ToEncryptSize) + 128))			\
			{																		\
				rOutput.Reallocate(rOutput.mBufferSize + (ToEncryptSize) + 128);	\
			}																		\
		}
	
	// Encode the chunk
	if(compressChunk)
	{
		// buffer to compress into
		uint8_t buffer[2048];
		
		// Set compressor with all the chunk as an input
		Compress<true> compress;
		compress.Input(Chunk, ChunkSize);
		compress.FinishInput();

		// Get and encrypt output
		while(!compress.OutputHasFinished())
		{
			int s = compress.Output(buffer, sizeof(buffer));
			if(s > 0)
			{
				ENCODECHUNK_CHECK_SPACE(s)
				outOffset += spEncrypt->Transform(rOutput.mpBuffer + outOffset, rOutput.mBufferSize - outOffset, buffer, s);				
			}
			else
			{
				// Should never happen, as we put all the input in in one go.
				// So if this happens, it means there's a logical problem somewhere
				THROW_EXCEPTION(BackupStoreException, Internal)
			}
		}
		ENCODECHUNK_CHECK_SPACE(16)
		outOffset += spEncrypt->Final(rOutput.mpBuffer + outOffset, rOutput.mBufferSize - outOffset);
	}
	else
	{
		// Straight encryption
		ENCODECHUNK_CHECK_SPACE(ChunkSize)
		outOffset += spEncrypt->Transform(rOutput.mpBuffer + outOffset, rOutput.mBufferSize - outOffset, Chunk, ChunkSize);
		ENCODECHUNK_CHECK_SPACE(16)
		outOffset += spEncrypt->Final(rOutput.mpBuffer + outOffset, rOutput.mBufferSize - outOffset);
	}
	
	ASSERT(outOffset < rOutput.mBufferSize);		// first check should have sorted this -- merely logic check

	return outOffset;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreFile::DecodeChunk(const void *, int, void *, int)
//		Purpose: Decode an encoded chunk -- use OutputBufferSizeForKnownOutputSize() to find
//				 the extra output buffer size needed before calling.
//				 See notes in EncodeChunk() for notes re alignment of the 
//				 encoded data.
//		Created: 8/12/03
//
// --------------------------------------------------------------------------
int BackupStoreFile::DecodeChunk(const void *Encoded, int EncodedSize, void *Output, int OutputSize)
{
	// Check alignment of the encoded block
	ASSERT((((uint32_t)(long)Encoded) % BACKUPSTOREFILE_CODING_BLOCKSIZE) == BACKUPSTOREFILE_CODING_OFFSET);

	// First check
	if(EncodedSize < 1)
	{
		THROW_EXCEPTION(BackupStoreException, BadEncodedChunk)
	}

	const uint8_t *input = (uint8_t*)Encoded;
	
	// Get header, make checks, etc
	uint8_t header = input[0];
	bool chunkCompressed = (header & HEADER_CHUNK_IS_COMPRESSED) == HEADER_CHUNK_IS_COMPRESSED;
	uint8_t encodingType = (header >> HEADER_ENCODING_SHIFT);
	if(encodingType != HEADER_BLOWFISH_ENCODING && encodingType != HEADER_AES_ENCODING)
	{
		THROW_EXCEPTION(BackupStoreException, ChunkHasUnknownEncoding)
	}
	
#ifndef HAVE_OLD_SSL
	// Choose cipher
	CipherContext &cipher((encodingType == HEADER_AES_ENCODING)?sAESDecrypt:sBlowfishDecrypt);
#else
	// AES not supported with this version of OpenSSL
	if(encodingType == HEADER_AES_ENCODING)
	{
		THROW_EXCEPTION(BackupStoreException, AEScipherNotSupportedByInstalledOpenSSL)
	}
	CipherContext &cipher(sBlowfishDecrypt);
#endif
	
	// Check enough space for header, an IV and one byte of input
	int ivLen = cipher.GetIVLength();
	if(EncodedSize < (1 + ivLen + 1))
	{
		THROW_EXCEPTION(BackupStoreException, BadEncodedChunk)
	}

	// Set IV in decrypt context, and start
	cipher.SetIV(input + 1);
	cipher.Begin();
	
	// Setup vars for code
	int inOffset = 1 + ivLen;
	uint8_t *output = (uint8_t*)Output;
	int outOffset = 0;

	// Do action
	if(chunkCompressed)
	{
		// Do things in chunks
		uint8_t buffer[2048];
		int inputBlockLen = cipher.InSizeForOutBufferSize(sizeof(buffer));
		
		// Decompressor
		Compress<false> decompress;
		
		while(inOffset < EncodedSize)
		{
			// Decrypt a block
			int bl = inputBlockLen;
			if(bl > (EncodedSize - inOffset)) bl = EncodedSize - inOffset;	// not too long
			int s = cipher.Transform(buffer, sizeof(buffer), input + inOffset, bl);
			inOffset += bl;
			
			// Decompress the decrypted data
			if(s > 0)
			{
				decompress.Input(buffer, s);
				int os = 0;
				do
				{
					os = decompress.Output(output + outOffset, OutputSize - outOffset);
					outOffset += os;
				} while(os > 0);
				
				// Check that there's space left in the output buffer -- there always should be
				if(outOffset >= OutputSize)
				{
					THROW_EXCEPTION(BackupStoreException, NotEnoughSpaceToDecodeChunk)
				}
			}
		}
		
		// Get any compressed data remaining in the cipher context and compression
		int s = cipher.Final(buffer, sizeof(buffer));
		decompress.Input(buffer, s);
		decompress.FinishInput();
		while(!decompress.OutputHasFinished())
		{
			int os = decompress.Output(output + outOffset, OutputSize - outOffset);
			outOffset += os;

			// Check that there's space left in the output buffer -- there always should be
			if(outOffset >= OutputSize)
			{
				THROW_EXCEPTION(BackupStoreException, NotEnoughSpaceToDecodeChunk)
			}
		}
	}
	else
	{
		// Easy decryption
		outOffset += cipher.Transform(output + outOffset, OutputSize - outOffset, input + inOffset, EncodedSize - inOffset);
		outOffset += cipher.Final(output + outOffset, OutputSize - outOffset);
	}
	
	return outOffset;
}



// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreFile::ReorderFileToStreamOrder(IOStream *, bool)
//		Purpose: Returns a stream which gives a Stream order version of the encoded file.
//				 If TakeOwnership == true, then the input stream will be deleted when the
//				 returned stream is deleted.
//				 The input stream must be seekable.
//		Created: 10/12/03
//
// --------------------------------------------------------------------------
std::auto_ptr<IOStream> BackupStoreFile::ReorderFileToStreamOrder(IOStream *pStream, bool TakeOwnership)
{
	ASSERT(pStream != 0);

	// Get the size of the file
	int64_t fileSize = pStream->BytesLeftToRead();
	if(fileSize == IOStream::SizeOfStreamUnknown)
	{
		THROW_EXCEPTION(BackupStoreException, StreamDoesntHaveRequiredFeatures)
	}

	// Read the header
	int bytesRead = 0;
	file_StreamFormat hdr;
	bool readBlock = pStream->ReadFullBuffer(&hdr, sizeof(hdr), &bytesRead);

	// Seek backwards to put the file pointer back where it was before we started this
	pStream->Seek(0 - bytesRead, IOStream::SeekType_Relative);

	// Check we got a block
	if(!readBlock)
	{
		// Couldn't read header -- assume file bad
		THROW_EXCEPTION(BackupStoreException, BadBackupStoreFile)
	}

	// Check magic number
	if(ntohl(hdr.mMagicValue) != OBJECTMAGIC_FILE_MAGIC_VALUE_V1
#ifndef BOX_DISABLE_BACKWARDS_COMPATIBILITY_BACKUPSTOREFILE
		&& ntohl(hdr.mMagicValue) != OBJECTMAGIC_FILE_MAGIC_VALUE_V0
#endif
		)
	{
		THROW_EXCEPTION(BackupStoreException, BadBackupStoreFile)
	}
	
	// Get number of blocks
	int64_t numBlocks = box_ntoh64(hdr.mNumBlocks);
	
	// Calculate where the block index will be, check it's reasonable
	int64_t blockIndexSize = ((numBlocks * sizeof(file_BlockIndexEntry)) + sizeof(file_BlockIndexHeader));
	int64_t blockIndexLoc = fileSize - blockIndexSize;
	if(blockIndexLoc < 0)
	{
		// Doesn't look good!
		THROW_EXCEPTION(BackupStoreException, BadBackupStoreFile)
	}
	
	// Build a reordered stream
	std::auto_ptr<IOStream> reordered(new ReadGatherStream(TakeOwnership));
	
	// Set it up...
	ReadGatherStream &rreordered(*((ReadGatherStream*)reordered.get()));
	int component = rreordered.AddComponent(pStream);
	// Send out the block index
	rreordered.AddBlock(component, blockIndexSize, true, blockIndexLoc);
	// And then the rest of the file
	rreordered.AddBlock(component, blockIndexLoc, true, 0);
		
	return reordered;
}



// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreFile::ResetStats()
//		Purpose: Reset the gathered statistics
//		Created: 20/1/04
//
// --------------------------------------------------------------------------
void BackupStoreFile::ResetStats()
{
	msStats.mBytesInEncodedFiles = 0;
	msStats.mBytesAlreadyOnServer = 0;
	msStats.mTotalFileStreamSize = 0;
}



// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreFile::CompareFileContentsAgainstBlockIndex(const char *, IOStream &)
//		Purpose: Compares the contents of a file against the checksums contained in the
//				 block index. Returns true if the checksums match, meaning the file is
//				 extremely likely to match the original. Will always consume the entire index.
//		Created: 21/1/04
//
// --------------------------------------------------------------------------
bool BackupStoreFile::CompareFileContentsAgainstBlockIndex(const char *Filename, IOStream &rBlockIndex, int Timeout)
{
	// is it a symlink?
	bool sourceIsSymlink = false;
	{
		struct stat st;
		if(::lstat(Filename, &st) == -1)
		{
			THROW_EXCEPTION(CommonException, OSFileError)
		}
		if((st.st_mode & S_IFMT) == S_IFLNK)
		{
			sourceIsSymlink = true;
		}
	}

	// Open file, if it's not a symlink
	std::auto_ptr<FileStream> in;
	if(!sourceIsSymlink)
	{
		in.reset(new FileStream(Filename));
	}
	
	// Read header
	file_BlockIndexHeader hdr;
	if(!rBlockIndex.ReadFullBuffer(&hdr, sizeof(hdr), 0 /* not interested in bytes read if this fails */, Timeout))
	{
		// Couldn't read header
		THROW_EXCEPTION(BackupStoreException, CouldntReadEntireStructureFromStream)
	}

	// Check magic
	if(hdr.mMagicValue != (int32_t)htonl(OBJECTMAGIC_FILE_BLOCKS_MAGIC_VALUE_V1)
#ifndef BOX_DISABLE_BACKWARDS_COMPATIBILITY_BACKUPSTOREFILE
		&& hdr.mMagicValue != (int32_t)htonl(OBJECTMAGIC_FILE_BLOCKS_MAGIC_VALUE_V0)
#endif
		)
	{
		THROW_EXCEPTION(BackupStoreException, BadBackupStoreFile)
	}

#ifndef BOX_DISABLE_BACKWARDS_COMPATIBILITY_BACKUPSTOREFILE
	bool isOldVersion = hdr.mMagicValue == (int32_t)htonl(OBJECTMAGIC_FILE_BLOCKS_MAGIC_VALUE_V0);
#endif

	// Get basic information
	int64_t numBlocks = box_ntoh64(hdr.mNumBlocks);
	uint64_t entryIVBase = box_ntoh64(hdr.mEntryIVBase);
	
	//TODO: Verify that these sizes look reasonable
	
	// setup
	void *data = 0;
	int32_t dataSize = -1;
	bool matches = true;
	int64_t totalSizeInBlockIndex = 0;
	
	try
	{	
		for(int64_t b = 0; b < numBlocks; ++b)
		{
			// Read an entry from the stream
			file_BlockIndexEntry entry;
			if(!rBlockIndex.ReadFullBuffer(&entry, sizeof(entry), 0 /* not interested in bytes read if this fails */, Timeout))
			{
				// Couldn't read entry
				THROW_EXCEPTION(BackupStoreException, CouldntReadEntireStructureFromStream)
			}	
		
			// Calculate IV for this entry
			uint64_t iv = entryIVBase;
			iv += b;
			iv = box_hton64(iv);
#ifndef BOX_DISABLE_BACKWARDS_COMPATIBILITY_BACKUPSTOREFILE
			if(isOldVersion)
			{
				// Reverse the IV for compatibility
				iv = box_swap64(iv);
			}
#endif
			sBlowfishDecryptBlockEntry.SetIV(&iv);			
			
			// Decrypt the encrypted section
			file_BlockIndexEntryEnc entryEnc;
			int sectionSize = sBlowfishDecryptBlockEntry.TransformBlock(&entryEnc, sizeof(entryEnc),
					entry.mEnEnc, sizeof(entry.mEnEnc));
			if(sectionSize != sizeof(entryEnc))
			{
				THROW_EXCEPTION(BackupStoreException, BlockEntryEncodingDidntGiveExpectedLength)
			}

			// Size of block
			int32_t blockClearSize = ntohl(entryEnc.mSize);
			if(blockClearSize < 0 || blockClearSize > (BACKUP_FILE_MAX_BLOCK_SIZE + 1024))
			{
				THROW_EXCEPTION(BackupStoreException, BadBackupStoreFile)
			}
			totalSizeInBlockIndex += blockClearSize;

			// Make sure there's enough memory allocated to load the block in
			if(dataSize < blockClearSize)
			{
				// Too small, free the block if it's already allocated
				if(data != 0)
				{
					::free(data);
					data = 0;
				}
				// Allocate a block
				data = ::malloc(blockClearSize + 128);
				if(data == 0)
				{
					throw std::bad_alloc();
				}
				dataSize = blockClearSize + 128;
			}
			
			// Load in the block from the file, if it's not a symlink
			if(!sourceIsSymlink)
			{
				if(in->Read(data, blockClearSize) != blockClearSize)
				{
					// Not enough data left in the file, can't possibly match
					matches = false;
				}
				else
				{
					// Check the checksum
					MD5Digest md5;
					md5.Add(data, blockClearSize);
					md5.Finish();
					if(!md5.DigestMatches(entryEnc.mStrongChecksum))
					{
						// Checksum didn't match
						matches = false;
					}
				}
			}
			
			// Keep on going regardless, to make sure the entire block index stream is read
			// -- must always be consistent about what happens with the stream.
		}
	}
	catch(...)
	{
		// clean up in case of errors
		if(data != 0)
		{
			::free(data);
			data = 0;
		}
		throw;
	}
	
	// free block
	if(data != 0)
	{
		::free(data);
		data = 0;
	}
	
	// Check for data left over if it's not a symlink
	if(!sourceIsSymlink)
	{
		// Anything left to read in the file?
		if(in->BytesLeftToRead() != 0)
		{
			// File has extra data at the end
			matches = false;
		}
	}
	
	// Symlinks must have zero size on server
	if(sourceIsSymlink)
	{
		matches = (totalSizeInBlockIndex == 0);
	}
	
	return matches;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreFile::EncodingBuffer::EncodingBuffer()
//		Purpose: Constructor
//		Created: 25/11/04
//
// --------------------------------------------------------------------------
BackupStoreFile::EncodingBuffer::EncodingBuffer()
	: mpBuffer(0),
	  mBufferSize(0)
{
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreFile::EncodingBuffer::~EncodingBuffer()
//		Purpose: Destructor
//		Created: 25/11/04
//
// --------------------------------------------------------------------------
BackupStoreFile::EncodingBuffer::~EncodingBuffer()
{
	if(mpBuffer != 0)
	{
		BackupStoreFile::CodingChunkFree(mpBuffer);
		mpBuffer = 0;
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreFile::EncodingBuffer::Allocate(int)
//		Purpose: Do initial allocation of block
//		Created: 25/11/04
//
// --------------------------------------------------------------------------
void BackupStoreFile::EncodingBuffer::Allocate(int Size)
{
	ASSERT(mpBuffer == 0);
	uint8_t *buffer = (uint8_t*)BackupStoreFile::CodingChunkAlloc(Size);
	if(buffer == 0)
	{
		throw std::bad_alloc();
	}
	mpBuffer = buffer;
	mBufferSize = Size;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    BackupStoreFile::EncodingBuffer::Reallocate(int)
//		Purpose: Reallocate the block. Try not to call this, it has to copy
//				 the entire contents as the block can't be reallocated straight.
//		Created: 25/11/04
//
// --------------------------------------------------------------------------
void BackupStoreFile::EncodingBuffer::Reallocate(int NewSize)
{
#ifndef WIN32
	TRACE2("Reallocating EncodingBuffer from %d to %d\n", mBufferSize, NewSize);
#endif
	ASSERT(mpBuffer != 0);
	uint8_t *buffer = (uint8_t*)BackupStoreFile::CodingChunkAlloc(NewSize);
	if(buffer == 0)
	{
		throw std::bad_alloc();
	}
	// Copy data
	::memcpy(buffer, mpBuffer, (NewSize > mBufferSize)?mBufferSize:NewSize);
	
	// Free old
	BackupStoreFile::CodingChunkFree(mpBuffer);
	
	// Store new buffer
	mpBuffer = buffer;
	mBufferSize = NewSize;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DiffTimer::DiffTimer();
//		Purpose: Constructor
//		Created: 2005/02/01
//
// --------------------------------------------------------------------------
DiffTimer::DiffTimer()
{
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    DiffTimer::DiffTimer();
//		Purpose: Destructor
//		Created: 2005/02/01
//
// --------------------------------------------------------------------------
DiffTimer::~DiffTimer()
{	
}
