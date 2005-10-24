// --------------------------------------------------------------------------
//
// File
//		Name:    RaidFileRead.cpp
//		Purpose: Read Raid like Files
//		Created: 2003/07/13
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <syslog.h>
#include <stdarg.h>
#include <dirent.h>

#include <stdio.h>
#include <string.h>
#include <memory>
#include <map>

#include "RaidFileRead.h"
#include "RaidFileException.h"
#include "RaidFileController.h"
#include "RaidFileUtil.h"

#ifdef PLATFORM_LINUX
	#include "LinuxWorkaround.h"
#endif

#include "MemLeakFindOn.h"

#define READ_NUMBER_DISCS_REQUIRED	3
#define READV_MAX_BLOCKS			64

// --------------------------------------------------------------------------
//
// Class
//		Name:    RaidFileRead_NonRaid
//		Purpose: Internal class for reading RaidFiles which haven't been transformed
//				 into the RAID like form yet.
//		Created: 2003/07/13
//
// --------------------------------------------------------------------------
class RaidFileRead_NonRaid : public RaidFileRead
{
public:
	RaidFileRead_NonRaid(int SetNumber, const std::string &Filename, int OSFileHandle);
	virtual ~RaidFileRead_NonRaid();
private:
	RaidFileRead_NonRaid(const RaidFileRead_NonRaid &rToCopy);

public:
	virtual int Read(void *pBuffer, int NBytes, int Timeout = IOStream::TimeOutInfinite);
	virtual pos_type GetPosition() const;
	virtual void Seek(IOStream::pos_type Offset, int SeekType);
	virtual void Close();
	virtual pos_type GetFileSize() const;
	virtual bool StreamDataLeft();

private:
	int mOSFileHandle;
	bool mEOF;
};

// --------------------------------------------------------------------------
//
// Function
//		Name:    RaidFileRead_NonRaid(int, const std::string &, const std::string &)
//		Purpose: Constructor
//		Created: 2003/07/13
//
// --------------------------------------------------------------------------
RaidFileRead_NonRaid::RaidFileRead_NonRaid(int SetNumber, const std::string &Filename, int OSFileHandle)
	: RaidFileRead(SetNumber, Filename),
	  mOSFileHandle(OSFileHandle),
	  mEOF(false)
{
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    RaidFileRead_NonRaid::~RaidFileRead_NonRaid()
//		Purpose: Destructor
//		Created: 2003/07/13
//
// --------------------------------------------------------------------------
RaidFileRead_NonRaid::~RaidFileRead_NonRaid()
{
	if(mOSFileHandle != -1)
	{
		Close();
	}
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    RaidFileRead_NonRaid::Read(const void *, int)
//		Purpose: Reads bytes from the file
//		Created: 2003/07/13
//
// --------------------------------------------------------------------------
int RaidFileRead_NonRaid::Read(void *pBuffer, int NBytes, int Timeout)
{
	// open?
	if(mOSFileHandle == -1)
	{
		THROW_EXCEPTION(RaidFileException, NotOpen)
	}
	
	// Read data
	int bytesRead = ::read(mOSFileHandle, pBuffer, NBytes);
	if(bytesRead == -1)
	{
		THROW_EXCEPTION(RaidFileException, OSError)
	}
	// Check for EOF
	if(bytesRead == 0)
	{
		mEOF = true;
	}

	return bytesRead;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    RaidFileRead_NonRaid::GetPosition()
//		Purpose: Returns current position
//		Created: 2003/07/13
//
// --------------------------------------------------------------------------
RaidFileRead::pos_type RaidFileRead_NonRaid::GetPosition() const
{
	// open?
	if(mOSFileHandle == -1)
	{
		THROW_EXCEPTION(RaidFileException, NotOpen)
	}
	
	// Use lseek to find the current file position
	off_t p = ::lseek(mOSFileHandle, 0, SEEK_CUR);
	if(p == -1)
	{
		THROW_EXCEPTION(RaidFileException, OSError)
	}

	return p;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    RaidFileRead_NonRaid::Seek(pos_type, int)
//		Purpose: Seek within the file
//		Created: 2003/07/13
//
// --------------------------------------------------------------------------
void RaidFileRead_NonRaid::Seek(IOStream::pos_type Offset, int SeekType)
{
	// open?
	if(mOSFileHandle == -1)
	{
		THROW_EXCEPTION(RaidFileException, NotOpen)
	}
	
	// Seek...
	if(::lseek(mOSFileHandle, Offset, ConvertSeekTypeToOSWhence(SeekType)) == -1)
	{
		THROW_EXCEPTION(RaidFileException, OSError)
	}

	// Not EOF any more
	mEOF = false;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    RaidFileRead_NonRaid::Close()
//		Purpose: Close the file (automatically done by destructor)
//		Created: 2003/07/13
//
// --------------------------------------------------------------------------
void RaidFileRead_NonRaid::Close()
{
	// open?
	if(mOSFileHandle == -1)
	{
		THROW_EXCEPTION(RaidFileException, NotOpen)
	}
	
	// Close file...
	if(::close(mOSFileHandle) != 0)
	{
		THROW_EXCEPTION(RaidFileException, OSError)
	}
	mOSFileHandle = -1;
	mEOF = true;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    RaidFileRead_NonRaid::GetFileSize()
//		Purpose: Returns file size.
//		Created: 2003/07/14
//
// --------------------------------------------------------------------------
RaidFileRead::pos_type RaidFileRead_NonRaid::GetFileSize() const
{
	// open?
	if(mOSFileHandle == -1)
	{
		THROW_EXCEPTION(RaidFileException, NotOpen)
	}
	
	// stat the file
	struct stat st;
	if(::fstat(mOSFileHandle, &st) != 0)
	{
		THROW_EXCEPTION(RaidFileException, OSError)
	}

	return st.st_size;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    RaidFileRead_NonRaid::StreamDataLeft()
//		Purpose: Any data left?
//		Created: 2003/08/21
//
// --------------------------------------------------------------------------
bool RaidFileRead_NonRaid::StreamDataLeft()
{
	return !mEOF;
}

// ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// --------------------------------------------------------------------------
//
// Class
//		Name:    RaidFileRead_Raid
//		Purpose: Internal class for reading RaidFiles have been transformed.
//		Created: 2003/07/13
//
// --------------------------------------------------------------------------
class RaidFileRead_Raid : public RaidFileRead
{
public:
	friend class RaidFileRead;
	RaidFileRead_Raid(int SetNumber, const std::string &Filename, int Stripe1Handle,
		int Stripe2Handle, int ParityHandle, pos_type FileSize, unsigned int BlockSize,
		bool LastBlockHasSize);
	virtual ~RaidFileRead_Raid();
private:
	RaidFileRead_Raid(const RaidFileRead_Raid &rToCopy);

public:
	virtual int Read(void *pBuffer, int NBytes, int Timeout = IOStream::TimeOutInfinite);
	virtual pos_type GetPosition() const;
	virtual void Seek(IOStream::pos_type Offset, int SeekType);
	virtual void Close();
	virtual pos_type GetFileSize() const;
	virtual bool StreamDataLeft();

private:
	int ReadRecovered(void *pBuffer, int NBytes);
	void AttemptToRecoverFromIOError(bool Stripe1);
	void SetPosition(pos_type FilePosition);
	static void MoveDamagedFileAlertDaemon(int SetNumber, const std::string &Filename, bool Stripe1);

private:
	int mStripe1Handle;
	int mStripe2Handle;
	int mParityHandle;
	pos_type mFileSize;
	unsigned int mBlockSize;
	pos_type mCurrentPosition;
	char *mRecoveryBuffer;
	pos_type mRecoveryBufferStart;
	bool mLastBlockHasSize;
	bool mEOF;
};

// --------------------------------------------------------------------------
//
// Function
//		Name:    RaidFileRead_Raid(int, const std::string &, const std::string &)
//		Purpose: Constructor
//		Created: 2003/07/13
//
// --------------------------------------------------------------------------
RaidFileRead_Raid::RaidFileRead_Raid(int SetNumber, const std::string &Filename, int Stripe1Handle, int Stripe2Handle, int ParityHandle, pos_type FileSize, unsigned int BlockSize, bool LastBlockHasSize)
	: RaidFileRead(SetNumber, Filename),
	  mStripe1Handle(Stripe1Handle),
	  mStripe2Handle(Stripe2Handle),
	  mParityHandle(ParityHandle),
	  mFileSize(FileSize),
	  mBlockSize(BlockSize),
	  mCurrentPosition(0),
	  mRecoveryBuffer(0),
	  mRecoveryBufferStart(-1),
	  mLastBlockHasSize(LastBlockHasSize),
	  mEOF(false)
{
	// Make sure size of the IOStream::pos_type matches the pos_type used
#ifdef PLATFORM_LINUX
	ASSERT(sizeof(pos_type) >= sizeof(off_t));
#else
	ASSERT(sizeof(pos_type) == sizeof(off_t));
#endif
	
	// Sanity check handles
	if(mStripe1Handle != -1 && mStripe2Handle != -1)
	{
		// Everything is lovely, got two perfect files
	}
	else
	{
		// Check we have at least one stripe and a parity file
		if((mStripe1Handle == -1 && mStripe2Handle == -1) || mParityHandle == -1)
		{
			// Should never have got this far
			THROW_EXCEPTION(RaidFileException, Internal)
		}
	}
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    RaidFileRead_Raid::~RaidFileRead_Raid()
//		Purpose: Destructor
//		Created: 2003/07/13
//
// --------------------------------------------------------------------------
RaidFileRead_Raid::~RaidFileRead_Raid()
{
	Close();
	if(mRecoveryBuffer != 0)
	{
		::free(mRecoveryBuffer);
	}
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    RaidFileRead_Raid::Read(const void *, int)
//		Purpose: Reads bytes from the file
//		Created: 2003/07/13
//
// --------------------------------------------------------------------------
int RaidFileRead_Raid::Read(void *pBuffer, int NBytes, int Timeout)
{
	// How many more bytes could we read?
	unsigned int maxRead = mFileSize - mCurrentPosition;
	if((unsigned int)NBytes > maxRead)
	{
		NBytes = maxRead;
	}
	
	// Return immediately if there's nothing to read, and set EOF
	if(NBytes == 0)
	{
		mEOF = true;
		return 0;
	}
	
	// Can we use the normal file reading routine?
	if(mStripe1Handle == -1 || mStripe2Handle == -1)
	{
		// File is damaged, try a the recovery read function
		return ReadRecovered(pBuffer, NBytes);
	}
	
	// Vectors for reading stuff from the files
	struct iovec stripe1Reads[READV_MAX_BLOCKS];
	struct iovec stripe2Reads[READV_MAX_BLOCKS];
	struct iovec *stripeReads[2] = {stripe1Reads, stripe2Reads};
	unsigned int stripeReadsDataSize[2] = {0, 0};
	unsigned int stripeReadsSize[2] = {0, 0};
	int stripeHandles[2] = {mStripe1Handle, mStripe2Handle};
	
	// Which block are we doing?
	unsigned int currentBlock = mCurrentPosition / mBlockSize;
	unsigned int bytesLeftInCurrentBlock = mBlockSize - (mCurrentPosition % mBlockSize);
	ASSERT(bytesLeftInCurrentBlock > 0)
	unsigned int leftToRead = NBytes;
	char *bufferPtr = (char*)pBuffer;
	
	// Now... add some whole block entries in...
	try
	{
		while(leftToRead > 0)
		{
			int whichStripe = (currentBlock & 1);
			size_t rlen = mBlockSize;
			// Adjust if it's the first block
			if(bytesLeftInCurrentBlock != 0)
			{
				rlen = bytesLeftInCurrentBlock;
				bytesLeftInCurrentBlock = 0;
			}
			// Adjust if we're out of bytes
			if(rlen > leftToRead)
			{
				rlen = leftToRead;
			}
			stripeReads[whichStripe][stripeReadsSize[whichStripe]].iov_base = bufferPtr;
			stripeReads[whichStripe][stripeReadsSize[whichStripe]].iov_len = rlen;
			stripeReadsSize[whichStripe]++;
			stripeReadsDataSize[whichStripe] += rlen;
			leftToRead -= rlen;
			bufferPtr += rlen;
			currentBlock++;

			// Read data?
			for(int s = 0; s < 2; ++s)
			{
				if((leftToRead == 0 || stripeReadsSize[s] >= READV_MAX_BLOCKS) && stripeReadsSize[s] > 0)
				{
					int r = ::readv(stripeHandles[s], stripeReads[s], stripeReadsSize[s]);
					if(r == -1)
					{
						// Bad news... IO error?
						if(errno == EIO)
						{
							// Attempt to recover from this failure
							AttemptToRecoverFromIOError((s == 0) /* is stripe 1 */);
							// Retry
							return Read(pBuffer, NBytes, Timeout);
						}
						else
						{
							// Can't do anything, throw
							THROW_EXCEPTION(RaidFileException, OSError)
						}
					}
					else if(r != (int)stripeReadsDataSize[s])
					{
						// Got the file sizes wrong/logic error!
						THROW_EXCEPTION(RaidFileException, Internal)
					}
					stripeReadsSize[s] = 0;
					stripeReadsDataSize[s] = 0;
				}
			}
		}
	}
	catch(...)
	{
		// Get file pointers to right place (to meet exception safe stuff)
		SetPosition(mCurrentPosition);
		
		throw;
	}
	
	// adjust current position
	mCurrentPosition += NBytes;

	return NBytes;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    RaidFileRead_Raid::MoveDamagedFileAlertDaemon(bool)
//		Purpose: Moves a file into the damaged directory, and alerts the Daemon to recover it properly later.
//		Created: 2003/07/22
//
// --------------------------------------------------------------------------
void RaidFileRead_Raid::MoveDamagedFileAlertDaemon(int SetNumber, const std::string &Filename, bool Stripe1)
{
	// Move the dodgy file away
	// Get the controller and the disc set we're on
	RaidFileController &rcontroller(RaidFileController::GetController());
	RaidFileDiscSet rdiscSet(rcontroller.GetDiscSet(SetNumber));
	if(READ_NUMBER_DISCS_REQUIRED != rdiscSet.size())
	{
		THROW_EXCEPTION(RaidFileException, WrongNumberOfDiscsInSet)
	}
	// Start disc
	int startDisc = rdiscSet.GetSetNumForWriteFiles(Filename);
	int errOnDisc = (startDisc + (Stripe1?0:1)) % READ_NUMBER_DISCS_REQUIRED;
	
	// Make a munged filename for renaming
	std::string mungeFn(Filename + RAIDFILE_EXTENSION);
	std::string awayName;
	for(std::string::const_iterator i = mungeFn.begin(); i != mungeFn.end(); ++i)
	{
		char c = (*i);
		if(c == DIRECTORY_SEPARATOR_ASCHAR)
		{
			awayName += '_';
		}
		else if(c == '_')
		{
			awayName += "__";
		}
		else
		{
			awayName += c;
		}
	}
	// Make sure the error files directory exists
	std::string dirname(rdiscSet[errOnDisc] + DIRECTORY_SEPARATOR ".raidfile-unreadable");
	int mdr = ::mkdir(dirname.c_str(), 0750);
	if(mdr != 0 && errno != EEXIST)
	{
		THROW_EXCEPTION(RaidFileException, OSError)
	}
	// Attempt to rename the file there -- ignore any return code here, as it's dubious anyway
	std::string errorFile(RaidFileUtil::MakeRaidComponentName(rdiscSet, Filename, errOnDisc));
	::rename(errorFile.c_str(), (dirname + DIRECTORY_SEPARATOR_ASCHAR + awayName).c_str());

	// TODO: Inform the recovery daemon
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    RaidFileRead_Raid::AttemptToRecoverFromIOError(bool)
//		Purpose: Attempt to recover from an IO error, setting up to read from parity instead.
//				 Will exception if this isn't possible.
//		Created: 2003/07/14
//
// --------------------------------------------------------------------------
void RaidFileRead_Raid::AttemptToRecoverFromIOError(bool Stripe1)
{
	TRACE3("Attempting to recover from I/O error: %d %s, on stripe %d\n", mSetNumber, mFilename.c_str(), Stripe1?1:2);
	::syslog(LOG_ERR | LOG_LOCAL5, "Attempting to recover from I/O error: %d %s, on stripe %d\n", mSetNumber, mFilename.c_str(), Stripe1?1:2);

	// Close offending file
	if(Stripe1)
	{
		if(mStripe1Handle != -1)
		{
			::close(mStripe1Handle);
			mStripe1Handle = -1;
		}
	}
	else
	{
		if(mStripe2Handle != -1)
		{
			::close(mStripe2Handle);
			mStripe2Handle = -1;
		}
	}

	// Check...
	ASSERT((Stripe1?mStripe2Handle:mStripe1Handle) != -1);

	// Get rid of the damaged file
	MoveDamagedFileAlertDaemon(mSetNumber, mFilename, Stripe1);

	// Get the controller and the disc set we're on
	RaidFileController &rcontroller(RaidFileController::GetController());
	RaidFileDiscSet rdiscSet(rcontroller.GetDiscSet(mSetNumber));
	if(READ_NUMBER_DISCS_REQUIRED != rdiscSet.size())
	{
		THROW_EXCEPTION(RaidFileException, WrongNumberOfDiscsInSet)
	}
	// Start disc
	int startDisc = rdiscSet.GetSetNumForWriteFiles(mFilename);

	// Mark as nothing in recovery buffer
	mRecoveryBufferStart = -1;
	
	// Seek to zero on the remaining file -- get to nice state
	if(::lseek(Stripe1?mStripe2Handle:mStripe1Handle, 0, SEEK_SET) == -1)
	{
		THROW_EXCEPTION(RaidFileException, OSError)
	}

	// Open the parity file
	std::string parityFilename(RaidFileUtil::MakeRaidComponentName(rdiscSet, mFilename, (2 + startDisc) % READ_NUMBER_DISCS_REQUIRED));
	mParityHandle = ::open(parityFilename.c_str(), O_RDONLY, 0555);
	if(mParityHandle == -1)
	{
		THROW_EXCEPTION(RaidFileException, OSError)
	}
	
	// Work out whether or not there's a size XORed into the last block
	unsigned int bytesInLastTwoBlocks = mFileSize % (mBlockSize * 2);
	if(bytesInLastTwoBlocks > mBlockSize && bytesInLastTwoBlocks < ((mBlockSize * 2) - sizeof(FileSizeType)))
	{
		// Yes, there's something to XOR in the last block
		mLastBlockHasSize = true;
	}
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    RaidFileRead_Raid::ReadRecovered(const void *, int)
//		Purpose: Reads data recreating from the parity stripe
//		Created: 2003/07/14
//
// --------------------------------------------------------------------------
int RaidFileRead_Raid::ReadRecovered(void *pBuffer, int NBytes)
{
	// Note: NBytes has been adjusted to definately be a range
	// inside the given file length.

	// Make sure a buffer is allocated
	if(mRecoveryBuffer == 0)
	{
		mRecoveryBuffer = (char*)::malloc(mBlockSize * 2);
		if(mRecoveryBuffer == 0)
		{
			throw std::bad_alloc();
		}
	}
	
	// Which stripe?
	int stripe = (mStripe1Handle != -1)?mStripe1Handle:mStripe2Handle;
	if(stripe == -1)
	{
		// Not enough file handles around
		THROW_EXCEPTION(RaidFileException, FileIsDamagedNotRecoverable)
	}
	
	char *outptr = (char*)pBuffer;
	int bytesToGo = NBytes;
	
	pos_type preservedCurrentPosition = mCurrentPosition;
	
	try
	{
		// Start offset within buffer
		int offset = (mCurrentPosition - mRecoveryBufferStart);
		// Let's go!
		while(bytesToGo > 0)
		{
			int bytesLeftInBuffer = 0;
			if(mRecoveryBufferStart != -1)
			{
				bytesLeftInBuffer = (mRecoveryBufferStart + (mBlockSize*2)) - mCurrentPosition;
				ASSERT(bytesLeftInBuffer >= 0);
			}
			
			// How many bytes can be copied out?
			int toCopy = bytesLeftInBuffer;
			if(toCopy > bytesToGo) toCopy = bytesToGo;
			//printf("offset = %d, tocopy = %d, bytestogo = %d, leftinbuffer = %d\n", (int)offset, toCopy, bytesToGo, bytesLeftInBuffer);
			if(toCopy > 0)
			{
				for(int l = 0; l < toCopy; ++l)
				{
					*(outptr++) = mRecoveryBuffer[offset++];
				}
				bytesToGo -= toCopy;
				mCurrentPosition += toCopy;
			}
			
			// Load in the next buffer?
			if(bytesToGo > 0)
			{
				// Calculate the blocks within the file that are needed to be loaded.
				pos_type fileBlock = mCurrentPosition / (mBlockSize * 2);
				// Is this the last block
				bool isLastBlock = (fileBlock == (mFileSize / (mBlockSize * 2)));
				
				// Need to reposition file pointers?
				if(mRecoveryBufferStart == -1)
				{
					// Yes!
					// And the offset from which to read it
					pos_type filePos = fileBlock * mBlockSize;
					// Then seek
					if(::lseek(stripe, filePos, SEEK_SET) == -1
						|| ::lseek(mParityHandle, filePos, SEEK_SET) == -1)
					{
						THROW_EXCEPTION(RaidFileException, OSError)
					}
				}
				
				// Load a block from each file, getting the ordering the right way round
				int r1 = ::read((mStripe1Handle != -1)?stripe:mParityHandle, mRecoveryBuffer, mBlockSize);
				int r2 = ::read((mStripe1Handle != -1)?mParityHandle:stripe, mRecoveryBuffer + mBlockSize, mBlockSize);				
				if(r1 == -1 || r2 == -1)
				{
					THROW_EXCEPTION(RaidFileException, OSError)
				}

				// error checking and manipulation
				if(isLastBlock)
				{
					// Allow not full reads, and append zeros if necessary to fill the space.
					int r1zeros = mBlockSize - r1;
					if(r1zeros > 0)
					{
						::memset(mRecoveryBuffer + r1, 0, r1zeros);
					}
					int r2zeros = mBlockSize - r2;
					if(r2zeros > 0)
					{
						::memset(mRecoveryBuffer + mBlockSize + r2, 0, r2zeros);
					}
					
					// if it's got the file size in it, XOR it off
					if(mLastBlockHasSize)
					{
						int sizeXorOffset = (mBlockSize - sizeof(FileSizeType)) + ((mStripe1Handle != -1)?mBlockSize:0);
						*((FileSizeType*)(mRecoveryBuffer + sizeXorOffset)) ^= ntoh64(mFileSize);
					}
				}
				else
				{
					// Must have got a full block, otherwise things are a bit bad here.
					if(r1 != (int)mBlockSize || r2 != (int)mBlockSize)
					{
						THROW_EXCEPTION(RaidFileException, InvalidRaidFile)
					}
				}
				
				// Go XORing!
				unsigned int *b1 = (unsigned int*)mRecoveryBuffer;
				unsigned int *b2 = (unsigned int *)(mRecoveryBuffer + mBlockSize);
				if((mStripe1Handle == -1))
				{
					b1 = b2;
					b2 = (unsigned int*)mRecoveryBuffer;
				}
				for(int x = ((mBlockSize/sizeof(unsigned int)) - 1); x >= 0; --x)
				{
					*b2 = (*b1) ^ (*b2);
					++b1;
					++b2;
				}
				
				// New block location
				mRecoveryBufferStart = fileBlock * (mBlockSize * 2);
				
				// New offset withing block
				offset = (mCurrentPosition - mRecoveryBufferStart);
				ASSERT(offset >= 0);
			}
		}
	}
	catch(...)
	{
		// Change variables so 1) buffer is invalidated and 2) the file will be seeked properly the next time round
		mRecoveryBufferStart = -1;
		mCurrentPosition = preservedCurrentPosition;
		throw;
	}
	
	return NBytes;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    RaidFileRead_Raid::GetPosition()
//		Purpose: Returns current position
//		Created: 2003/07/13
//
// --------------------------------------------------------------------------
IOStream::pos_type RaidFileRead_Raid::GetPosition() const
{
	return mCurrentPosition;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    RaidFileRead_Raid::Seek(RaidFileRead::pos_type, bool)
//		Purpose: Seek within the file
//		Created: 2003/07/13
//
// --------------------------------------------------------------------------
void RaidFileRead_Raid::Seek(IOStream::pos_type Offset, int SeekType)
{
	pos_type newpos = mCurrentPosition;
	switch(SeekType)
	{
	case IOStream::SeekType_Absolute:
		newpos = Offset;
		break;
		
	case IOStream::SeekType_Relative:
		newpos += Offset;
		break;
		
	case IOStream::SeekType_End:
		newpos = mFileSize + Offset;
		break;
		
	default:
		THROW_EXCEPTION(CommonException, IOStreamBadSeekType)
	}
	
	if(newpos != mCurrentPosition)
	{
		SetPosition(newpos);
	}
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    RaidFileRead_Raid::SetPosition(pos_type)
//		Purpose: Move the file pointers
//		Created: 2003/07/14
//
// --------------------------------------------------------------------------
void RaidFileRead_Raid::SetPosition(pos_type FilePosition)
{
	if(FilePosition > mFileSize)
	{
		FilePosition = mFileSize;
	}

	if(mStripe1Handle != -1 && mStripe2Handle != -1)
	{
		// right then... which block is it in?
		pos_type block = FilePosition / mBlockSize;
		pos_type offset = FilePosition % mBlockSize;
		
		// Calculate offsets for each file
		pos_type basepos = (block / 2) * mBlockSize;
		pos_type s1p, s2p;
		if((block & 1) == 0)
		{
			s1p = basepos + offset;
			s2p = basepos;
		}
		else
		{
			s1p = basepos + mBlockSize;
			s2p = basepos + offset;
		}
		// Note: lseek isn't in the man pages to return EIO, but assuming that it can return this,
		// as it calls various OS bits and returns their error codes, and those fns look like they might.
		if(::lseek(mStripe1Handle, s1p, SEEK_SET) == -1)
		{
			if(errno == EIO)
			{
				TRACE3("I/O error when seeking in %d %s (to %d), stripe 1\n", mSetNumber, mFilename.c_str(), (int)FilePosition);
				::syslog(LOG_ERR | LOG_LOCAL5, "I/O error when seeking in %d %s (to %d), stripe 1\n", mSetNumber, mFilename.c_str(), (int)FilePosition);
				// Attempt to recover
				AttemptToRecoverFromIOError(true /* is stripe 1 */);
				ASSERT(mStripe1Handle == -1);
				// Retry
				SetPosition(FilePosition);
				return;
			}
			else
			{
				THROW_EXCEPTION(RaidFileException, OSError)
			}
		}
		if(::lseek(mStripe2Handle, s2p, SEEK_SET) == -1)
		{
			if(errno == EIO)
			{
				TRACE3("I/O error when seeking in %d %s (to %d), stripe 2\n", mSetNumber, mFilename.c_str(), (int)FilePosition);
				::syslog(LOG_ERR | LOG_LOCAL5, "I/O error when seeking in %d %s (to %d), stripe 2\n", mSetNumber, mFilename.c_str(), (int)FilePosition);
				// Attempt to recover
				AttemptToRecoverFromIOError(false /* is stripe 2 */);
				ASSERT(mStripe2Handle == -1);
				// Retry
				SetPosition(FilePosition);
				return;
			}
			else
			{
				THROW_EXCEPTION(RaidFileException, OSError)
			}
		}
		
		// Store position
		mCurrentPosition = FilePosition;
	}
	else
	{
		// Simply store, and mark the recovery buffer invalid
		mCurrentPosition = FilePosition;
		mRecoveryBufferStart = -1;
	}

	// not EOF any more
	mEOF = false;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    RaidFileRead_Raid::Close()
//		Purpose: Close the file (automatically done by destructor)
//		Created: 2003/07/13
//
// --------------------------------------------------------------------------
void RaidFileRead_Raid::Close()
{
	if(mStripe1Handle != -1)
	{
		::close(mStripe1Handle);
		mStripe1Handle = -1;
	}
	if(mStripe2Handle != -1)
	{
		::close(mStripe2Handle);
		mStripe2Handle = -1;
	}
	if(mParityHandle != -1)
	{
		::close(mParityHandle);
		mParityHandle = -1;
	}
	
	mEOF = true;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    RaidFileRead_NonRaid::StreamDataLeft()
//		Purpose: Any data left?
//		Created: 2003/08/21
//
// --------------------------------------------------------------------------
bool RaidFileRead_Raid::StreamDataLeft()
{
	return !mEOF;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    RaidFileRead_Raid::GetFileSize()
//		Purpose: Returns file size.
//		Created: 2003/07/14
//
// --------------------------------------------------------------------------
RaidFileRead::pos_type RaidFileRead_Raid::GetFileSize() const
{
	return mFileSize;
}


// ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



// --------------------------------------------------------------------------
//
// Function
//		Name:    RaidFileRead::RaidFileRead(int, const std::string &)
//		Purpose: Constructor
//		Created: 2003/07/13
//
// --------------------------------------------------------------------------
RaidFileRead::RaidFileRead(int SetNumber, const std::string &Filename)
	: mSetNumber(SetNumber),
	  mFilename(Filename)
{
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    RaidFileRead::~RaidFileRead()
//		Purpose: Destructor
//		Created: 2003/07/13
//
// --------------------------------------------------------------------------
RaidFileRead::~RaidFileRead()
{
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    RaidFileRead::Open(int, const std::string &, int)
//		Purpose: Opens a RaidFile for reading.
//		Created: 2003/07/13
//
// --------------------------------------------------------------------------
std::auto_ptr<RaidFileRead> RaidFileRead::Open(int SetNumber, const std::string &Filename, int64_t *pRevisionID, int BufferSizeHint)
{
	// See what's available...
	// Get disc set
	RaidFileController &rcontroller(RaidFileController::GetController());
	RaidFileDiscSet rdiscSet(rcontroller.GetDiscSet(SetNumber));
	if(READ_NUMBER_DISCS_REQUIRED != rdiscSet.size() && 1 != rdiscSet.size()) // allow non-RAID configurations
	{
		THROW_EXCEPTION(RaidFileException, WrongNumberOfDiscsInSet)
	}

	// See if the file exists
	int startDisc = 0, existingFiles = 0;
	RaidFileUtil::ExistType existance = RaidFileUtil::RaidFileExists(rdiscSet, Filename, &startDisc, &existingFiles, pRevisionID);
	if(existance == RaidFileUtil::NoFile)
	{
		THROW_EXCEPTION(RaidFileException, RaidFileDoesntExist)
	}
	else if(existance == RaidFileUtil::NonRaid)
	{
		// Simple non-RAID file so far...
	
		// Get the filename for the write file
		std::string writeFilename(RaidFileUtil::MakeWriteFileName(rdiscSet, Filename));

		// Attempt to open
		int osFileHandle = ::open(writeFilename.c_str(), O_RDONLY, 0);
		if(osFileHandle == -1)
		{
			THROW_EXCEPTION(RaidFileException, ErrorOpeningFileForRead)
		}
		
		// Return a read object for this file
		try
		{
			return std::auto_ptr<RaidFileRead>(new RaidFileRead_NonRaid(SetNumber, Filename, osFileHandle));
		}
		catch(...)
		{
			::close(osFileHandle);
			throw;
		}
	}
	else if(existance == RaidFileUtil::AsRaid
		|| ((existingFiles & RaidFileUtil::Stripe1Exists) && (existingFiles & RaidFileUtil::Stripe2Exists)))
	{
		if(existance != RaidFileUtil::AsRaid)
		{
			TRACE2("Opening %d %s in normal mode, but parity file doesn't exist\n", SetNumber, Filename.c_str());
			::syslog(LOG_ERR | LOG_LOCAL5, "Opening %d %s in normal mode, but parity file doesn't exist\n", SetNumber, Filename.c_str());
			// TODO: Alert recovery daemon
		}
	
		// Open the two stripe files
		std::string stripe1Filename(RaidFileUtil::MakeRaidComponentName(rdiscSet, Filename, (0 + startDisc) % READ_NUMBER_DISCS_REQUIRED));
		std::string stripe2Filename(RaidFileUtil::MakeRaidComponentName(rdiscSet, Filename, (1 + startDisc) % READ_NUMBER_DISCS_REQUIRED));
		int stripe1 = -1;
		int stripe1errno = 0;
		int stripe2 = -1;
		int stripe2errno = 0;
		
		try
		{
			// Open stripe1
			stripe1 = ::open(stripe1Filename.c_str(), O_RDONLY, 0555);
			if(stripe1 == -1)
			{
				stripe1errno = errno;
			}
			// Open stripe2
			stripe2 = ::open(stripe2Filename.c_str(), O_RDONLY, 0555);
			if(stripe2 == -1)
			{
				stripe2errno = errno;
			}
			if(stripe1errno != 0 || stripe2errno != 0)
			{
				THROW_EXCEPTION(RaidFileException, ErrorOpeningFileForRead)
			}
			
			// stat stripe 1 to find ('half' of) length...
			struct stat st;
			if(::fstat(stripe1, &st) != 0)
			{
				stripe1errno = errno;
			}
			pos_type length = st.st_size;
			
			// stat stripe2 to find (other 'half' of) length...
			if(::fstat(stripe2, &st) != 0)
			{
				stripe2errno = errno;
			}
			length += st.st_size;
			
			// Handle errors
			if(stripe1errno != 0 || stripe2errno != 0)
			{
				THROW_EXCEPTION(RaidFileException, OSError)
			}
			
			// Make a nice object to represent this file
			return std::auto_ptr<RaidFileRead>(new RaidFileRead_Raid(SetNumber, Filename, stripe1, stripe2, -1, length, rdiscSet.GetBlockSize(), false /* actually we don't know */));
		}
		catch(...)
		{
			// Close open files
			if(stripe1 != -1)
			{
				::close(stripe1);
				stripe1 = -1;
			}
			if(stripe2 != -1)
			{
				::close(stripe2);
				stripe2 = -1;
			}
			
			// Now... maybe we can try again with one less file?
			bool oktotryagain = true;
			if(stripe1errno == EIO)
			{
				TRACE2("I/O error on opening %d %s stripe 1, trying recovery mode\n", SetNumber, Filename.c_str());
				::syslog(LOG_ERR | LOG_LOCAL5, "I/O error on opening %d %s stripe 1, trying recovery mode\n", SetNumber, Filename.c_str());
				RaidFileRead_Raid::MoveDamagedFileAlertDaemon(SetNumber, Filename, true /* is stripe 1 */);

				existingFiles = existingFiles & ~RaidFileUtil::Stripe1Exists;
				existance = (existance == RaidFileUtil::AsRaidWithMissingReadable)
					?RaidFileUtil::AsRaidWithMissingNotRecoverable
					:RaidFileUtil::AsRaidWithMissingReadable;
			}
			else if(stripe1errno != 0)
			{
				oktotryagain = false;
			}
			
			if(stripe2errno == EIO)
			{
				TRACE2("I/O error on opening %d %s stripe 2, trying recovery mode\n", SetNumber, Filename.c_str());
				::syslog(LOG_ERR | LOG_LOCAL5, "I/O error on opening %d %s stripe 2, trying recovery mode\n", SetNumber, Filename.c_str());
				RaidFileRead_Raid::MoveDamagedFileAlertDaemon(SetNumber, Filename, false /* is stripe 2 */);

				existingFiles = existingFiles & ~RaidFileUtil::Stripe2Exists;
				existance = (existance == RaidFileUtil::AsRaidWithMissingReadable)
					?RaidFileUtil::AsRaidWithMissingNotRecoverable
					:RaidFileUtil::AsRaidWithMissingReadable;
			}
			else if(stripe2errno != 0)
			{
				oktotryagain = false;
			}
			
			if(!oktotryagain)
			{
				throw;
			}
		}
	}

	if(existance == RaidFileUtil::AsRaidWithMissingReadable)
	{
		TRACE3("Attempting to open RAID file %d %s in recovery mode (stripe %d present)\n", SetNumber, Filename.c_str(), (existingFiles & RaidFileUtil::Stripe1Exists)?1:2);
		::syslog(LOG_ERR | LOG_LOCAL5, "Attempting to open RAID file %d %s in recovery mode (stripe %d present)\n", SetNumber, Filename.c_str(), (existingFiles & RaidFileUtil::Stripe1Exists)?1:2);
	
		// Generate the filenames of all the lovely files
		std::string stripe1Filename(RaidFileUtil::MakeRaidComponentName(rdiscSet, Filename, (0 + startDisc) % READ_NUMBER_DISCS_REQUIRED));
		std::string stripe2Filename(RaidFileUtil::MakeRaidComponentName(rdiscSet, Filename, (1 + startDisc) % READ_NUMBER_DISCS_REQUIRED));
		std::string parityFilename(RaidFileUtil::MakeRaidComponentName(rdiscSet, Filename, (2 + startDisc) % READ_NUMBER_DISCS_REQUIRED));

		int stripe1 = -1;
		int stripe2 = -1;
		int parity = -1;

		try
		{
			// Open stripe1?
			if(existingFiles & RaidFileUtil::Stripe1Exists)
			{
				stripe1 = ::open(stripe1Filename.c_str(), O_RDONLY, 0555);
				if(stripe1 == -1)
				{
					THROW_EXCEPTION(RaidFileException, OSError)
				}
			}
			// Open stripe2?
			if(existingFiles & RaidFileUtil::Stripe2Exists)
			{
				stripe2 = ::open(stripe2Filename.c_str(), O_RDONLY, 0555);
				if(stripe2 == -1)
				{
					THROW_EXCEPTION(RaidFileException, OSError)
				}
			}
			// Open parity
			parity = ::open(parityFilename.c_str(), O_RDONLY, 0555);
			if(parity == -1)
			{
				THROW_EXCEPTION(RaidFileException, OSError)
			}
			
			// Find the length. This is slightly complex.
			unsigned int blockSize = rdiscSet.GetBlockSize();
			pos_type length = 0;
			
			// The easy one... if the parity file is of an integral block size + sizeof(FileSizeType)
			// then it's stored at the end of the parity file
			struct stat st;
			if(::fstat(parity, &st) != 0)
			{
				THROW_EXCEPTION(RaidFileException, OSError)
			}
			pos_type paritySize = st.st_size;
			FileSizeType parityLastData = 0;
			bool parityIntegralPlusOffT = ((paritySize % blockSize) == sizeof(FileSizeType));
			if(paritySize >= static_cast<pos_type>(sizeof(parityLastData)) && (parityIntegralPlusOffT || stripe1 != -1))
			{
				// Seek to near the end
				ASSERT(sizeof(FileSizeType) == 8); // compiler bug (I think) prevents from using 0 - sizeof(FileSizeType)...
				if(::lseek(parity, -8 /*(0 - sizeof(FileSizeType))*/, SEEK_END) == -1)
				{
					THROW_EXCEPTION(RaidFileException, OSError)
				}
				// Read it in
				if(::read(parity, &parityLastData, sizeof(parityLastData)) != sizeof(parityLastData))
				{
					THROW_EXCEPTION(RaidFileException, OSError)
				}
				// Set back to beginning of file
				if(::lseek(parity, 0, SEEK_SET) == -1)
				{
					THROW_EXCEPTION(RaidFileException, OSError)
				}
			}
			
			bool lastBlockHasSize = false;
			if(parityIntegralPlusOffT)
			{
				// Wonderful! Have the value
				length = ntoh64(parityLastData);
			}
			else
			{
				// Have to resort to more devious means.
				if(existingFiles & RaidFileUtil::Stripe1Exists)
				{
					// Procedure for stripe 1 existence...
					// 	Get size of stripe1.
					//  If this is not an integral block size, then size can use this
					//  to work out the size of the file.
					//  Otherwise, read in the end of the last block, and use a bit of XORing
					//  to get the size from the FileSizeType value at end of the file.
					if(::fstat(stripe1, &st) != 0)
					{
						THROW_EXCEPTION(RaidFileException, OSError)
					}
					pos_type stripe1Size = st.st_size;
					// Is size integral?
					if((stripe1Size % ((pos_type)blockSize)) != 0)
					{
						// No, so know the size.
						length = stripe1Size + ((stripe1Size / blockSize) * blockSize);
					}
					else
					{
						// Must read the last bit of data from the block and XOR.
						FileSizeType stripe1LastData = 0;	// initialise to zero, as we may not read everything from it
						
						// Work out how many bytes to read
						int btr = 0;			// bytes to read off end
						unsigned int lbs = stripe1Size % blockSize;
						if(lbs == 0 && stripe1Size > 0)
						{
							// integral size, need the entire bit
							btr = sizeof(FileSizeType);
						}
						else if(lbs > (blockSize - sizeof(FileSizeType)))
						{
							btr = lbs - (blockSize - sizeof(FileSizeType));
						}
						
						// Seek to near the end
						if(btr > 0)
						{
							ASSERT(sizeof(FileSizeType) == 8); // compiler bug (I think) prevents from using 0 - sizeof(FileSizeType)...
							ASSERT(btr <= (int)sizeof(FileSizeType));
							if(::lseek(stripe1, 0 - btr, SEEK_END) == -1)
							{
								THROW_EXCEPTION(RaidFileException, OSError)
							}
							// Read it in
							if(::read(stripe1, &stripe1LastData, btr) != btr)
							{
								THROW_EXCEPTION(RaidFileException, OSError)
							}
							// Set back to beginning of file
							if(::lseek(stripe1, 0, SEEK_SET) == -1)
							{
								THROW_EXCEPTION(RaidFileException, OSError)
							}
						}
						// Lovely!
						length = stripe1LastData ^ parityLastData;
						// Convert to host byte order
						length = ntoh64(length);
						ASSERT(length <= (paritySize + stripe1Size));
						// Mark is as having this to aid code later
						lastBlockHasSize = true;
					}
				}
				else
				{
					ASSERT(existingFiles & RaidFileUtil::Stripe2Exists);
				}

				if(existingFiles & RaidFileUtil::Stripe2Exists)
				{
					// Get size of stripe2 file
					if(::fstat(stripe2, &st) != 0)
					{
						THROW_EXCEPTION(RaidFileException, OSError)
					}
					pos_type stripe2Size = st.st_size;
					
					// Is it an integral size?
					if(stripe2Size % blockSize != 0)
					{
						// No. Working out the size is easy.
						length = stripe2Size + (((stripe2Size / blockSize)+1) * blockSize);
						// Got last block size in there?
						if((stripe2Size % blockSize) <= static_cast<pos_type>((blockSize - sizeof(pos_type))))
						{
							// Yes...
							lastBlockHasSize = true;
						}
					}
					else
					{
						// Yes. So we need to compare with the parity file to get a clue...
						pos_type stripe2Blocks = stripe2Size / blockSize;
						pos_type parityBlocks = paritySize / blockSize;
						if(stripe2Blocks == parityBlocks)
						{
							// Same size, so stripe1 must be the same size
							length = (stripe2Blocks * 2) * blockSize;
						}
						else
						{
							// Different size, so stripe1 must be one block bigger
							ASSERT(stripe2Blocks < parityBlocks);
							length = ((stripe2Blocks * 2)+1) * blockSize;
						}
						
						// Then... add in the extra bit of the parity length
						unsigned int lastBlockSize = paritySize % blockSize;
						length += lastBlockSize;
					}
				}
				else
				{
					ASSERT(existingFiles & RaidFileUtil::Stripe1Exists);
				}
			}

			// Create a lovely object to return
			return std::auto_ptr<RaidFileRead>(new RaidFileRead_Raid(SetNumber, Filename, stripe1, stripe2, parity, length, blockSize, lastBlockHasSize));
		}
		catch(...)
		{
			// Close open files
			if(stripe1 != -1)
			{
				::close(stripe1);
				stripe1 = -1;
			}
			if(stripe2 != -1)
			{
				::close(stripe2);
				stripe2 = -1;
			}
			if(parity != -1)
			{
				::close(parity);
				parity = -1;
			}
			throw;
		}
	}
	
	THROW_EXCEPTION(RaidFileException, FileIsDamagedNotRecoverable)
	
	// Avoid compiler warning -- it'll never get here...
	return std::auto_ptr<RaidFileRead>();
}




// --------------------------------------------------------------------------
//
// Function
//		Name:    RaidFileRead::DirectoryExists(int, const std::string &)
//		Purpose: Returns true if the directory exists. Throws exception if it's partially in existence.
//		Created: 2003/08/20
//
// --------------------------------------------------------------------------
bool RaidFileRead::DirectoryExists(int SetNumber, const std::string &rDirName)
{
	// Get disc set
	RaidFileController &rcontroller(RaidFileController::GetController());
	RaidFileDiscSet rdiscSet(rcontroller.GetDiscSet(SetNumber));

	return DirectoryExists(rdiscSet, rDirName);
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    RaidFileRead::DirectoryExists(const RaidFileDiscSet &, const std::string &)
//		Purpose: Returns true if the directory exists. Throws exception if it's partially in existence.
//		Created: 2003/08/20
//
// --------------------------------------------------------------------------
bool RaidFileRead::DirectoryExists(const RaidFileDiscSet &rSet, const std::string &rDirName)
{
	// For each directory, test to see if it exists
	unsigned int nexist = 0;
	for(unsigned int l = 0; l < rSet.size(); ++l)
	{
		// build name
		std::string dn(rSet[l] + DIRECTORY_SEPARATOR + rDirName);
		
		// check for existence
		struct stat st;
		if(::stat(dn.c_str(), &st) == 0)
		{
			// Directory?
			if(st.st_mode & S_IFDIR)
			{
				// yes
				nexist++;
			}
			else
			{
				// No. It's a file. Bad!
				THROW_EXCEPTION(RaidFileException, UnexpectedFileInDirPlace)
			}
		}
		else
		{
			// Was it a non-exist error?
			if(errno != ENOENT)
			{
				// No. Bad things.
				THROW_EXCEPTION(RaidFileException, OSError)
			}
		}
	}
	
	// Were all of them found?
	if(nexist == 0)
	{
		// None.
		return false;
	}
	else if(nexist == rSet.size())
	{
		// All
		return true;
	}

	// Some exist. We don't like this -- it shows something bad happened before
	// TODO: notify recovery daemon
	THROW_EXCEPTION(RaidFileException, DirectoryIncomplete)
	return false;	// avoid compiler warning
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    RaidFileRead::FileExists(int, const std::string &, int64_t *)
//		Purpose: Does a Raid file exist? Optionally return a revision number, which is
//				 unique to this saving of the file. (revision number may change
//				 after transformation to RAID -- so only use for cache control,
//				 not detecting changes to content).
//		Created: 2003/09/02
//
// --------------------------------------------------------------------------
bool RaidFileRead::FileExists(int SetNumber, const std::string &rFilename, int64_t *pRevisionID)
{
	// Get disc set
	RaidFileController &rcontroller(RaidFileController::GetController());
	RaidFileDiscSet rdiscSet(rcontroller.GetDiscSet(SetNumber));

	return RaidFileUtil::RaidFileExists(rdiscSet, rFilename, 0, 0, pRevisionID) != RaidFileUtil::NoFile;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    ReadDirectoryContents(int, const std::string &, int, std::vector<std::string> &)
//		Purpose: Read directory contents, returning whether or not all entries are likely to be readable or not
//		Created: 2003/08/20
//
// --------------------------------------------------------------------------
bool RaidFileRead::ReadDirectoryContents(int SetNumber, const std::string &rDirName, int DirReadType, std::vector<std::string> &rOutput)
{
	// Remove anything in the vector to begin with.
	rOutput.clear();
	
	// Controller and set
	RaidFileController &rcontroller(RaidFileController::GetController());
	RaidFileDiscSet rdiscSet(rcontroller.GetDiscSet(SetNumber));

	// Collect the directory listings
	std::map<std::string, unsigned int> counts;
	
	unsigned int numDiscs = rdiscSet.size();
	
	for(unsigned int l = 0; l < numDiscs; ++l)
	{
		// build name
		std::string dn(rdiscSet[l] + DIRECTORY_SEPARATOR + rDirName);
		
		// read the contents...
		DIR *dirHandle = 0;
		try
		{
			dirHandle = ::opendir(dn.c_str());
			if(dirHandle == 0)
			{
				THROW_EXCEPTION(RaidFileException, OSError)
			}
			
			struct dirent *en = 0;
			while((en = ::readdir(dirHandle)) != 0)
			{
#ifdef PLATFORM_LINUX
			LinuxWorkaround_FinishDirentStruct(en, dn.c_str());
#endif

				if(en->d_name[0] == '.' && 
					(en->d_name[1] == '\0' || (en->d_name[1] == '.' && en->d_name[2] == '\0')))
				{
					// ignore, it's . or ..
					continue;
				}
				
				// stat the file to find out what type it is
/*				struct stat st;
				std::string fullName(dn + DIRECTORY_SEPARATOR + en->d_name);
				if(::stat(fullName.c_str(), &st) != 0)
				{
					THROW_EXCEPTION(RaidFileException, OSError)
				}*/
				
				// Entry...
				std::string name;
				unsigned int countToAdd = 1;
				if(DirReadType == DirReadType_FilesOnly && en->d_type == DT_REG) // (st.st_mode & S_IFDIR) == 0)
				{
					// File. Complex, need to check the extension
					int dot = -1;
					int p = 0;
					while(en->d_name[p] != '\0')
					{
						if(en->d_name[p] == '.')
						{
							// store location of dot
							dot = p;
						}
						++p;
					}
					// p is length of string
					if(dot != -1 && ((p - dot) == 3 || (p - dot) == 4)
						&& en->d_name[dot+1] == 'r' && en->d_name[dot+2] == 'f'
						&& (en->d_name[dot+3] == 'w' || en->d_name[dot+3] == '\0'))
					{
						// so has right extension
						name.assign(en->d_name, dot);	/* get name up to last . */
						// Was it a write file (which counts as everything)
						if(en->d_name[dot+3] == 'w')
						{
							countToAdd = numDiscs;
						}
					}
				}
				if(DirReadType == DirReadType_DirsOnly && en->d_type == DT_DIR) // (st.st_mode & S_IFDIR))
				{
					// Directory, and we want directories
					name = en->d_name;
				}
				// Eligable for entry?
				if(!name.empty())
				{
					// add to map...
					std::map<std::string, unsigned int>::iterator i = counts.find(name);
					if(i != counts.end())
					{
						// add to count
						i->second += countToAdd;
					}
					else
					{
						// insert into map
						counts[name] = countToAdd;
					}
				}
			}
			
			if(::closedir(dirHandle) != 0)
			{
				THROW_EXCEPTION(RaidFileException, OSError)
			}
			dirHandle = 0;
		}
		catch(...)
		{
			if(dirHandle != 0)
			{
				::closedir(dirHandle);
			}
			throw;
		}
	}
	
	// Now go through the map, adding in entries
	bool everythingReadable = true;
	
	for(std::map<std::string, unsigned int>::const_iterator i = counts.begin(); i != counts.end(); ++i)
	{
		if(i->second < (numDiscs - 1))
		{
			// Too few discs to be confident of reading everything
			everythingReadable = false;
		}
		
		// Add name to vector
		rOutput.push_back(i->first);
	}
	
	return everythingReadable;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    RaidFileRead::Write(const void *, int)
//		Purpose: Not support, throws exception
//		Created: 2003/08/21
//
// --------------------------------------------------------------------------
void RaidFileRead::Write(const void *pBuffer, int NBytes)
{
	THROW_EXCEPTION(RaidFileException, UnsupportedReadWriteOrClose)
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    RaidFileRead::StreamClosed()
//		Purpose: Never any data to write
//		Created: 2003/08/21
//
// --------------------------------------------------------------------------
bool RaidFileRead::StreamClosed()
{
	return true;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    RaidFileRead::BytesLeftToRead()
//		Purpose: Can tell how many bytes there are to go
//		Created: 2003/08/26
//
// --------------------------------------------------------------------------
IOStream::pos_type RaidFileRead::BytesLeftToRead()
{
	return GetFileSize() - GetPosition();
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    RaidFileRead::GetDiscUsageInBlocks()
//		Purpose: Return how many blocks are used.
//		Created: 2003/09/03
//
// --------------------------------------------------------------------------
IOStream::pos_type RaidFileRead::GetDiscUsageInBlocks()
{
	RaidFileController &rcontroller(RaidFileController::GetController());
	RaidFileDiscSet rdiscSet(rcontroller.GetDiscSet(mSetNumber));
	return RaidFileUtil::DiscUsageInBlocks(GetFileSize(), rdiscSet);
}




