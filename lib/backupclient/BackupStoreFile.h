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

#include "IOStream.h"
#include "BackupClientFileAttributes.h"
#include "BackupStoreFilename.h"

#include <memory>

typedef struct 
{
	int64_t mBytesInEncodedFiles;
	int64_t mBytesAlreadyOnServer;
	int64_t mTotalFileStreamSize;
} BackupStoreFileStats;

// --------------------------------------------------------------------------
//
// Class
//		Name:    BackupStoreFile
//		Purpose: Class to hold together utils for maniplating files.
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
		virtual void Write(const void *pBuffer, int NBytes);
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


	// Main interface
	static std::auto_ptr<IOStream> EncodeFile(const char *Filename, int64_t ContainerID, const BackupStoreFilename &rStoreFilename, int64_t *pModificationTime = 0);
	static std::auto_ptr<IOStream> EncodeFileDiff(const char *Filename, int64_t ContainerID,
		const BackupStoreFilename &rStoreFilename, int64_t DiffFromObjectID, IOStream &rDiffFromBlockIndex,
		int Timeout, int64_t *pModificationTime = 0, bool *pIsCompletelyDifferent = 0);
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
#ifndef PLATFORM_OLD_OPENSSL
	static void SetAESKey(const void *pKey, int KeyLength);
#endif

	// Limits
	static void SetMaximumDiffingTime(int Seconds);

	// Building blocks
	static int MaxBlockSizeForChunkSize(int ChunkSize);
	static int EncodeChunk(const void *Chunk, int ChunkSize, void *Output, int OutputSize);

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
#ifndef NDEBUG
	static bool TraceDetailsOfDiffProcess;
#endif

	// For decoding encoded files
	static void DumpFile(void *clibFileHandle, bool ToTrace, IOStream &rFile);
};

#endif // BACKUPSTOREFILE__H

