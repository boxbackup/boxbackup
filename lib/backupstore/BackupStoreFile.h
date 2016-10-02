// --------------------------------------------------------------------------
//
// File
//		Name:    BackupStoreFile.h
//		Purpose: Utils for manipulating files
//		Created: 2003/08/28
//
// --------------------------------------------------------------------------

#ifndef BACKUPSTOREFILE__H
#define BACKUPSTOREFILE__H

#include <cstdlib>
#include <memory>
#include <cstdlib>

#include "autogen_BackupProtocol.h"
#include "BackupClientFileAttributes.h"
#include "BackupStoreFileWire.h"
#include "BackupStoreFilename.h"
#include "CollectInBufferStream.h"
#include "IOStream.h"
#include "ReadLoggingStream.h"

typedef struct 
{
	int64_t mBytesInEncodedFiles;
	int64_t mBytesAlreadyOnServer;
	int64_t mTotalFileStreamSize;
} BackupStoreFileStats;

class BackgroundTask;
class RunStatusProvider;

// Uncomment to disable backwards compatibility
//#define BOX_DISABLE_BACKWARDS_COMPATIBILITY_BACKUPSTOREFILE


// Output buffer to EncodeChunk and input data to DecodeChunk must
// have specific alignment, see function comments.
#define BACKUPSTOREFILE_CODING_BLOCKSIZE		16
#define BACKUPSTOREFILE_CODING_OFFSET			15

// Have some memory allocation commands, note closing "Off" at end of file.
#include "MemLeakFindOn.h"

class BackupStoreFileEncodeStream;

// --------------------------------------------------------------------------
//
// Class
//		Name:    DiffTimer
//		Purpose: Interface for classes that can keep track of diffing time,
//				 and send SSL keepalive messages
//		Created: 2006/01/19
//
// --------------------------------------------------------------------------
class DiffTimer
{
public:
	DiffTimer();
	virtual ~DiffTimer();
public:
	virtual void DoKeepAlive() = 0;
	virtual int  GetMaximumDiffingTime() = 0;
	virtual bool IsManaged() = 0;
};

// --------------------------------------------------------------------------
//
// Class
//		Name:    BackupStoreFile
//		Purpose: Class to hold together utils for manipulating files.
//		Created: 2003/08/28
//
// --------------------------------------------------------------------------
class BackupStoreFile
{
public:
	class DecodedStream : public IOStream
	{
		friend class BackupStoreFile;
	private:
		DecodedStream(IOStream &rEncodedFile, int Timeout);
		DecodedStream(const DecodedStream &); // not allowed
		DecodedStream &operator=(const DecodedStream &); // not allowed
	public:
		~DecodedStream();

		// Stream functions
		virtual int Read(void *pBuffer, int NBytes, int Timeout);
		virtual void Write(const void *pBuffer, int NBytes,
			int Timeout = IOStream::TimeOutInfinite);
		virtual bool StreamDataLeft();
		virtual bool StreamClosed();
		
		// Accessor functions
		const BackupClientFileAttributes &GetAttributes() {return mAttributes;}
		const BackupStoreFilename &GetFilename() {return mFilename;}
		int64_t GetNumBlocks() {return mNumBlocks;}	// primarily for tests
		
		bool IsSymLink();
		
	private:
		void Setup(const BackupClientFileAttributes *pAlterativeAttr);
		void ReadBlockIndex(bool MagicAlreadyRead);
			
	private:
		IOStream &mrEncodedFile;
		int mTimeout;
		BackupClientFileAttributes mAttributes;
		BackupStoreFilename mFilename;
		int64_t mNumBlocks;
		void *mpBlockIndex;
		uint8_t *mpEncodedData;
		uint8_t *mpClearData;
		int mClearDataSize;
		int mCurrentBlock;
		int mCurrentBlockClearSize;
		int mPositionInCurrentBlock;
		uint64_t mEntryIVBase;
#ifndef BOX_DISABLE_BACKWARDS_COMPATIBILITY_BACKUPSTOREFILE
		bool mIsOldVersion;
#endif
	};

	class VerifyStream : public IOStream
	{
	private:
		enum
		{
			State_Header = 0,
			State_FilenameHeader,
			State_Filename,
			State_AttributesSize,
			State_Attributes,
			State_Blocks,
		};

		int mState;
		IOStream& mReadFromStream;
		CollectInBufferStream mCurrentUnitData;
		size_t mCurrentUnitSize;
		int64_t mNumBlocks;
		int64_t mBlockIndexSize;
		int64_t mCurrentPosition;
		int64_t mBlockDataPosition;
		bool mCurrentBufferIsAlternate;
		CollectInBufferStream mAlternateData;
		bool mBlockFromOtherFileReferenced;
		int64_t mContainerID;
		int64_t mDiffFromObjectID;
		bool mClosed;

	public:
		VerifyStream(IOStream& ReadFromStream)
		: mState(State_Header),
		  mReadFromStream(ReadFromStream),
		  mCurrentUnitSize(sizeof(file_StreamFormat)),
		  mNumBlocks(0),
		  mBlockIndexSize(0),
		  mCurrentPosition(0),
		  mBlockDataPosition(0),
		  mCurrentBufferIsAlternate(false),
		  mBlockFromOtherFileReferenced(false),
		  mContainerID(0),
		  mDiffFromObjectID(0),
		  mClosed(false)
		{ }
		virtual int Read(void *pBuffer, int NBytes,
			int Timeout = IOStream::TimeOutInfinite);
		virtual void Write(const void *pBuffer, int NBytes,
			int Timeout = IOStream::TimeOutInfinite)
		{
			THROW_EXCEPTION(CommonException, NotSupported);
		}
		// Declare twice (with different parameters) to avoid warnings that
		// Close(bool) hides overloaded virtual function.
		virtual void Close(bool CloseReadFromStream)
		{
			if(CloseReadFromStream)
			{
				mReadFromStream.Close();
			}
			Close();
		}
		virtual void Close();
		virtual bool StreamDataLeft()
		{
			return mReadFromStream.StreamDataLeft();
		}
		virtual bool StreamClosed()
		{
			return mClosed;
		}
	};

	// Main interface
	static std::auto_ptr<BackupStoreFileEncodeStream> EncodeFile
	(
		const std::string& Filename,
		int64_t ContainerID, const BackupStoreFilename &rStoreFilename,
		int64_t *pModificationTime = 0,
		ReadLoggingStream::Logger* pLogger = NULL,
		RunStatusProvider* pRunStatusProvider = NULL,
		BackgroundTask* pBackgroundTask = NULL
	);
	static std::auto_ptr<BackupStoreFileEncodeStream> EncodeFileDiff
	(
		const std::string& Filename, int64_t ContainerID,
		const BackupStoreFilename &rStoreFilename, 
		int64_t DiffFromObjectID, IOStream &rDiffFromBlockIndex,
		int Timeout, 
		DiffTimer *pDiffTimer,
		int64_t *pModificationTime = 0, 
		bool *pIsCompletelyDifferent = 0,
		BackgroundTask* pBackgroundTask = NULL
	);
	// Shortcut interface
	static int64_t QueryStoreFileDiff(BackupProtocolCallable& protocol,
		const std::string& LocalFilename, int64_t DirectoryObjectID,
		int64_t DiffFromFileID, int64_t AttributesHash,
		const BackupStoreFilenameClear& StoreFilename,
		int Timeout = IOStream::TimeOutInfinite,
		DiffTimer *pDiffTimer = NULL,
		ReadLoggingStream::Logger* pLogger = NULL,
		RunStatusProvider* pRunStatusProvider = NULL);

	static bool VerifyEncodedFileFormat(IOStream &rFile, int64_t *pDiffFromObjectIDOut = 0, int64_t *pContainerIDOut = 0);
	static void CombineFile(IOStream &rDiff, IOStream &rDiff2, IOStream &rFrom, IOStream &rOut);
	static void CombineDiffs(IOStream &rDiff1, IOStream &rDiff2, IOStream &rDiff2b, IOStream &rOut);
	static void ReverseDiffFile(IOStream &rDiff, IOStream &rFrom, IOStream &rFrom2, IOStream &rOut, int64_t ObjectIDOfFrom, bool *pIsCompletelyDifferent = 0);
	static void DecodeFile(IOStream &rEncodedFile, const char *DecodedFilename, int Timeout, const BackupClientFileAttributes *pAlterativeAttr = 0);
	static std::auto_ptr<BackupStoreFile::DecodedStream> DecodeFileStream(IOStream &rEncodedFile, int Timeout, const BackupClientFileAttributes *pAlterativeAttr = 0);
	static bool CompareFileContentsAgainstBlockIndex(const char *Filename, IOStream &rBlockIndex, int Timeout);
	static std::auto_ptr<IOStream> CombineFileIndices(IOStream &rDiff, IOStream &rFrom, bool DiffIsIndexOnly = false, bool FromIsIndexOnly = false);

	// Stream manipulation
	static std::auto_ptr<IOStream> ReorderFileToStreamOrder(IOStream *pStream, bool TakeOwnership);
	static void MoveStreamPositionToBlockIndex(IOStream &rStream);

	// Crypto setup
	static void SetBlowfishKeys(const void *pKey, int KeyLength, const void *pBlockEntryKey, int BlockEntryKeyLength);
#ifndef HAVE_OLD_SSL
	static void SetAESKey(const void *pKey, int KeyLength);
#endif

	// Allocation of properly aligning chunks for decoding and encoding chunks
	inline static void *CodingChunkAlloc(int Size)
	{
		uint8_t *a = (uint8_t*)malloc((Size) + (BACKUPSTOREFILE_CODING_BLOCKSIZE * 3));
		if(a == 0) return 0;
		// Align to main block size
		ASSERT(sizeof(uint64_t) >= sizeof(void*));	// make sure casting the right pointer size
		uint8_t adjustment = BACKUPSTOREFILE_CODING_BLOCKSIZE
			  - (uint8_t)(((uint64_t)a) % BACKUPSTOREFILE_CODING_BLOCKSIZE);
		uint8_t *b = (a + adjustment);
		// Store adjustment
		*b = adjustment;
		// Return offset
		return b + BACKUPSTOREFILE_CODING_OFFSET;
	}
	inline static void CodingChunkFree(void *Block)
	{
		// Check alignment is as expected
		ASSERT(sizeof(uint64_t) >= sizeof(void*));	// make sure casting the right pointer size
		ASSERT((uint8_t)(((uint64_t)Block) % BACKUPSTOREFILE_CODING_BLOCKSIZE) == BACKUPSTOREFILE_CODING_OFFSET);
		uint8_t *a = (uint8_t*)Block;
		a -= BACKUPSTOREFILE_CODING_OFFSET;
		// Adjust downwards...
		a -= *a;
		free(a);
	}

	static void DiffTimerExpired();

	// Building blocks
	class EncodingBuffer
	{
	public:
		EncodingBuffer();
		~EncodingBuffer();
	private:
		// No copying
		EncodingBuffer(const EncodingBuffer &);
		EncodingBuffer &operator=(const EncodingBuffer &);
	public:
		void Allocate(int Size);
		void Reallocate(int NewSize);
		
		uint8_t *mpBuffer;
		int mBufferSize;
	};
	static int MaxBlockSizeForChunkSize(int ChunkSize);
	static int EncodeChunk(const void *Chunk, int ChunkSize, BackupStoreFile::EncodingBuffer &rOutput);

	// Caller should know how big the output size is, but also allocate a bit more memory to cover various
	// overheads allowed for in checks
	static inline int OutputBufferSizeForKnownOutputSize(int KnownChunkSize)
	{
		// Plenty big enough
		return KnownChunkSize + 256;
	}
	static int DecodeChunk(const void *Encoded, int EncodedSize, void *Output, int OutputSize);

	// Statisitics, not designed to be completely reliable	
	static void ResetStats();
	static BackupStoreFileStats msStats;
	
	// For debug
#ifndef BOX_RELEASE_BUILD
	static bool TraceDetailsOfDiffProcess;
#endif

	// For decoding encoded files
	static void DumpFile(void *clibFileHandle, bool ToTrace, IOStream &rFile);
};

#include "MemLeakFindOff.h"

#endif // BACKUPSTOREFILE__H
