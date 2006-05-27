// --------------------------------------------------------------------------
//
// File
//		Name:    RaidFileWrite.cpp
//		Purpose: Writing RAID like files
//		Created: 2003/07/10
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/file.h>

#include <stdio.h>
#include <string.h>

#include "Guards.h"
#include "RaidFileWrite.h"
#include "RaidFileController.h"
#include "RaidFileException.h"
#include "RaidFileUtil.h"
#include "Utils.h"
// For DirectoryExists fn
#include "RaidFileRead.h"

#include "MemLeakFindOn.h"

// should be a multiple of 2
#define TRANSFORM_BLOCKS_TO_LOAD		4
// Must have this number of discs in the set
#define TRANSFORM_NUMBER_DISCS_REQUIRED	3

// --------------------------------------------------------------------------
//
// Function
//		Name:    RaidFileWrite::RaidFileWrite(int, const std::string &)
//		Purpose: Construtor, just stores requried details
//		Created: 2003/07/10
//
// --------------------------------------------------------------------------
RaidFileWrite::RaidFileWrite(int SetNumber, const std::string &Filename)
	: mSetNumber(SetNumber),
	  mFilename(Filename),
	  mOSFileHandle(-1)		// not valid file handle
{
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    RaidFileWrite::~RaidFileWrite()
//		Purpose: Destructor (will discard written file if not commited)
//		Created: 2003/07/10
//
// --------------------------------------------------------------------------
RaidFileWrite::~RaidFileWrite()
{
	if(mOSFileHandle != -1)
	{
		Discard();
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    RaidFileWrite::Open()
//		Purpose: Opens the file for writing
//		Created: 2003/07/10
//
// --------------------------------------------------------------------------
void RaidFileWrite::Open(bool AllowOverwrite)
{
	if(mOSFileHandle != -1)
	{
		THROW_EXCEPTION(RaidFileException, AlreadyOpen)
	}
	
	// Get disc set
	RaidFileController &rcontroller(RaidFileController::GetController());
	RaidFileDiscSet rdiscSet(rcontroller.GetDiscSet(mSetNumber));

	// Check for overwriting? (step 1)
	if(!AllowOverwrite)
	{
		// See if the file exists already -- can't overwrite existing files
		RaidFileUtil::ExistType existance = RaidFileUtil::RaidFileExists(rdiscSet, mFilename);
		if(existance != RaidFileUtil::NoFile)
		{
			TRACE2("Trying to overwrite raidfile %d %s\n", mSetNumber, mFilename.c_str());
			THROW_EXCEPTION(RaidFileException, CannotOverwriteExistingFile)
		}
	}

	// Get the filename for the write file
	std::string writeFilename(RaidFileUtil::MakeWriteFileName(rdiscSet, mFilename));
	// Add on a temporary extension
	writeFilename += 'X';

	// Attempt to open
	mOSFileHandle = ::open(writeFilename.c_str(), 
		O_WRONLY | O_CREAT | O_BINARY,
		S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
	if(mOSFileHandle == -1)
	{
		THROW_EXCEPTION(RaidFileException, ErrorOpeningWriteFile)
	}
	
	// Get a lock on the write file
#ifdef HAVE_FLOCK
	int errnoBlock = EWOULDBLOCK;
	if(::flock(mOSFileHandle, LOCK_EX | LOCK_NB) != 0)
#elif HAVE_DECL_F_SETLK
	int errnoBlock = EAGAIN;
	struct flock desc;
	desc.l_type = F_WRLCK;
	desc.l_whence = SEEK_SET;
	desc.l_start = 0;
	desc.l_len = 0;
	if(::fcntl(mOSFileHandle, F_SETLK, &desc) != 0)
#else
	int errnoBlock = ENOSYS;
	if (0)
#endif
	{
		// Lock was not obtained.
		bool wasLocked = (errno == errnoBlock);
		// Close the file
		::close(mOSFileHandle);
		mOSFileHandle = -1;
		// Report an exception?
		if(wasLocked)
		{
			THROW_EXCEPTION(RaidFileException, FileIsCurrentlyOpenForWriting)
		}
		else
		{
			// Random error occured
			THROW_EXCEPTION(RaidFileException, OSError)
		}
	}
	
	// Truncate it to size zero
	if(::ftruncate(mOSFileHandle, 0) != 0)
	{
		THROW_EXCEPTION(RaidFileException, ErrorOpeningWriteFileOnTruncate)
	}
	
	// Done!
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    RaidFileWrite::Write(const void *, int)
//		Purpose: Writes a block of data
//		Created: 2003/07/10
//
// --------------------------------------------------------------------------
void RaidFileWrite::Write(const void *pBuffer, int Length)
{
	// open?
	if(mOSFileHandle == -1)
	{
		THROW_EXCEPTION(RaidFileException, NotOpen)
	}
	
	// Write data
	int written = ::write(mOSFileHandle, pBuffer, Length);
	if(written != Length)
	{
		TRACE3("RaidFileWrite::Write: Write failure, Length = %d, written = %d, errno = %d\n", Length, written, errno);
		THROW_EXCEPTION(RaidFileException, OSError)
	}
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    RaidFileWrite::GetPosition()
//		Purpose: Returns current position in file
//		Created: 2003/07/10
//
// --------------------------------------------------------------------------
IOStream::pos_type RaidFileWrite::GetPosition() const
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
//		Name:    RaidFileWrite::Seek(RaidFileWrite::pos_type, bool)
//		Purpose: Seeks in the file, relative to current position if Relative is true.
//		Created: 2003/07/10
//
// --------------------------------------------------------------------------
void RaidFileWrite::Seek(IOStream::pos_type SeekTo, int SeekType)
{
	// open?
	if(mOSFileHandle == -1)
	{
		THROW_EXCEPTION(RaidFileException, NotOpen)
	}
	
	// Seek...
	if(::lseek(mOSFileHandle, SeekTo, ConvertSeekTypeToOSWhence(SeekType)) == -1)
	{
		THROW_EXCEPTION(RaidFileException, OSError)
	}
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    RaidFileWrite::Commit(bool)
//		Purpose: Closes, and commits the written file
//		Created: 2003/07/10
//
// --------------------------------------------------------------------------
void RaidFileWrite::Commit(bool ConvertToRaidNow)
{
	// open?
	if(mOSFileHandle == -1)
	{
		THROW_EXCEPTION(RaidFileException, NotOpen)
	}
	
	// Rename it into place -- BEFORE it's closed so lock remains

#ifdef WIN32
	// Except on Win32 which doesn't allow renaming open files
	// Close file...
	if(::close(mOSFileHandle) != 0)
	{
		THROW_EXCEPTION(RaidFileException, OSError)
	}
	mOSFileHandle = -1;
#endif // WIN32

	RaidFileController &rcontroller(RaidFileController::GetController());
	RaidFileDiscSet rdiscSet(rcontroller.GetDiscSet(mSetNumber));
	// Get the filename for the write file
	std::string renameTo(RaidFileUtil::MakeWriteFileName(rdiscSet, mFilename));
	// And the current name
	std::string renameFrom(renameTo + 'X');

#ifdef WIN32
	// need to delete the target first
	if(::unlink(renameTo.c_str()) != 0 && 
		GetLastError() != ERROR_FILE_NOT_FOUND)
	{
		THROW_EXCEPTION(RaidFileException, OSError)
	}
#endif

	if(::rename(renameFrom.c_str(), renameTo.c_str()) != 0)
	{
		THROW_EXCEPTION(RaidFileException, OSError)
	}

#ifndef WIN32	
	// Close file...
	if(::close(mOSFileHandle) != 0)
	{
		THROW_EXCEPTION(RaidFileException, OSError)
	}
	mOSFileHandle = -1;
#endif // !WIN32
	
	// Raid it?
	if(ConvertToRaidNow)
	{
		TransformToRaidStorage();
	}
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    RaidFileWrite::Discard()
//		Purpose: Closes, discarding the data written.
//		Created: 2003/07/10
//
// --------------------------------------------------------------------------
void RaidFileWrite::Discard()
{
	// open?
	if(mOSFileHandle == -1)
	{
		THROW_EXCEPTION(RaidFileException, NotOpen)
	}

	// Get disc set
	RaidFileController &rcontroller(RaidFileController::GetController());
	RaidFileDiscSet rdiscSet(rcontroller.GetDiscSet(mSetNumber));

	// Get the filename for the write file (temporary)
	std::string writeFilename(RaidFileUtil::MakeWriteFileName(rdiscSet, mFilename));
	writeFilename += 'X';
	
	// Unlink and close it

#ifdef WIN32
	// On Win32 we must close it first
	if (::close(mOSFileHandle) != 0 ||
		::unlink(writeFilename.c_str()) != 0)
#else // !WIN32
	if (::unlink(writeFilename.c_str()) != 0 ||
		::close(mOSFileHandle) != 0))
#endif // !WIN32
	{
		THROW_EXCEPTION(RaidFileException, OSError)
	}

	// reset file handle
	mOSFileHandle = -1;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    RaidFileWrite::TransformToRaidStorage()
//		Purpose: Turns the file into the RAID storage form
//		Created: 2003/07/11
//
// --------------------------------------------------------------------------
void RaidFileWrite::TransformToRaidStorage()
{
	// open?
	if(mOSFileHandle != -1)
	{
		THROW_EXCEPTION(RaidFileException, WriteFileOpenOnTransform)
	}

	// Get disc set
	RaidFileController &rcontroller(RaidFileController::GetController());
	RaidFileDiscSet rdiscSet(rcontroller.GetDiscSet(mSetNumber));
	if(rdiscSet.IsNonRaidSet())
	{
		// Not in RAID mode -- do nothing
		return;
	}
	// Otherwise check that it's the right sized set
	if(TRANSFORM_NUMBER_DISCS_REQUIRED != rdiscSet.size())
	{
		THROW_EXCEPTION(RaidFileException, WrongNumberOfDiscsInSet)
	}
	unsigned int blockSize = rdiscSet.GetBlockSize();

	// Get the filename for the write file (and get the disc set name for the start disc)
	int startDisc = 0;
	std::string writeFilename(RaidFileUtil::MakeWriteFileName(rdiscSet, mFilename, &startDisc));
	
	// Open it
	FileHandleGuard<> writeFile(writeFilename.c_str());

	// Get file information for write file	
	struct stat writeFileStat;
	if(::fstat(writeFile, &writeFileStat) != 0)
	{
		THROW_EXCEPTION(RaidFileException, OSError)
	}
//	// DEBUG MODE -- check file system size block size is same as block size for files
//	// doesn't really apply, as space benefits of using fragment size are worth efficiency,
//	// and anyway, it'll be buffered eventually so it won't matter.
//	#ifndef NDEBUG
//	{
//		if(writeFileStat.st_blksize != blockSize)
//		{
//			TRACE2("TransformToRaidStorage: optimal block size of file = %d, of set = %d, MISMATCH\n",
//					writeFileStat.st_blksize, blockSize);
//		}
//	}
//	#endif
	
	// How many blocks is the file? (rounding up)
	int writeFileSizeInBlocks = (writeFileStat.st_size + (blockSize - 1)) / blockSize;
	// And how big should the buffer be? (round up to multiple of 2, and no bigger than the preset limit)
	int bufferSizeBlocks = (writeFileSizeInBlocks + 1) & ~1;
	if(bufferSizeBlocks > TRANSFORM_BLOCKS_TO_LOAD) bufferSizeBlocks = TRANSFORM_BLOCKS_TO_LOAD;
	// How big should the buffer be?
	int bufferSize = (TRANSFORM_BLOCKS_TO_LOAD * blockSize);
	
	// Allocate buffer...
	MemoryBlockGuard<char*> buffer(bufferSize);
	
	// Allocate buffer for parity file
	MemoryBlockGuard<char*> parityBuffer(blockSize);
	
	// Get filenames of eventual files
	std::string stripe1Filename(RaidFileUtil::MakeRaidComponentName(rdiscSet, mFilename, (startDisc + 0) % TRANSFORM_NUMBER_DISCS_REQUIRED));
	std::string stripe2Filename(RaidFileUtil::MakeRaidComponentName(rdiscSet, mFilename, (startDisc + 1) % TRANSFORM_NUMBER_DISCS_REQUIRED));
	std::string parityFilename(RaidFileUtil::MakeRaidComponentName(rdiscSet, mFilename, (startDisc + 2) % TRANSFORM_NUMBER_DISCS_REQUIRED));
	// Make write equivalents
	std::string stripe1FilenameW(stripe1Filename + 'P');
	std::string stripe2FilenameW(stripe2Filename + 'P');
	std::string parityFilenameW(parityFilename + 'P');
	
	
	// Then open them all for writing (in strict order)
	try
	{
#if HAVE_DECL_O_EXLOCK
		FileHandleGuard<(O_WRONLY | O_CREAT | O_EXCL | O_EXLOCK | O_BINARY)> stripe1(stripe1FilenameW.c_str());
		FileHandleGuard<(O_WRONLY | O_CREAT | O_EXCL | O_EXLOCK | O_BINARY)> stripe2(stripe2FilenameW.c_str());
		FileHandleGuard<(O_WRONLY | O_CREAT | O_EXCL | O_EXLOCK | O_BINARY)> parity(parityFilenameW.c_str());
#else
		FileHandleGuard<(O_WRONLY | O_CREAT | O_EXCL | O_BINARY)> stripe1(stripe1FilenameW.c_str());
		FileHandleGuard<(O_WRONLY | O_CREAT | O_EXCL | O_BINARY)> stripe2(stripe2FilenameW.c_str());
		FileHandleGuard<(O_WRONLY | O_CREAT | O_EXCL | O_BINARY)> parity(parityFilenameW.c_str());
#endif

		// Then... read in data...
		int bytesRead = -1;
		bool sizeRecordRequired = false;
		int blocksDone = 0;
		while((bytesRead = ::read(writeFile, buffer, bufferSize)) > 0)
		{
			// Blocks to do...
			int blocksToDo = (bytesRead + (blockSize - 1)) / blockSize;

			// Need to add zeros to end?
			int blocksRoundUp = (blocksToDo + 1) & ~1;
			int zerosEnd = (blocksRoundUp * blockSize);
			if(bytesRead != zerosEnd)
			{
				// Set the end of the blocks to zero
				::memset(buffer + bytesRead, 0, zerosEnd - bytesRead);
			}

			// number of int's to XOR
			unsigned int num = blockSize / sizeof(unsigned int);

			// Then... calculate and write parity data
			for(int b = 0; b < blocksToDo; b += 2)
			{
				// Calculate int pointers
				unsigned int *pstripe1 = (unsigned int *)(buffer + (b * blockSize));
				unsigned int *pstripe2 = (unsigned int *)(buffer + ((b+1) * blockSize));
				unsigned int *pparity = (unsigned int *)((char*)parityBuffer);

				// Do XOR
				for(unsigned int n = 0; n < num; ++n)
				{
					pparity[n] = pstripe1[n] ^ pstripe2[n];
				}
				
				// Size of parity to write...
				int parityWriteSize = blockSize;
				
				// Adjust if it's the last block
				if((blocksDone + (b + 2)) >= writeFileSizeInBlocks)
				{
					// Yes...
					unsigned int bytesInLastTwoBlocks = bytesRead - (b * blockSize);
					
					// Some special cases...
					// Zero will never happen... but in the (imaginary) case it does, the file size will be appended
					// by the test at the end.
					if(bytesInLastTwoBlocks == sizeof(RaidFileRead::FileSizeType)
						|| bytesInLastTwoBlocks == blockSize)
					{
						// Write the entire block, and put the file size at end
						sizeRecordRequired = true;
					}
					else if(bytesInLastTwoBlocks < blockSize)
					{
						// write only these bits
						parityWriteSize = bytesInLastTwoBlocks;
					}
					else if(bytesInLastTwoBlocks < ((blockSize * 2) - sizeof(RaidFileRead::FileSizeType)))
					{
						// XOR in the size at the end of the parity block
						ASSERT(sizeof(RaidFileRead::FileSizeType) == (2*sizeof(unsigned int)));
						ASSERT(sizeof(RaidFileRead::FileSizeType) >= sizeof(off_t));
						int sizePos = (blockSize/sizeof(unsigned int)) - 2;
						union { RaidFileRead::FileSizeType l; unsigned int i[2]; } sw;

						sw.l = box_hton64(writeFileStat.st_size);
						pparity[sizePos+0] = pstripe1[sizePos+0] ^ sw.i[0];
						pparity[sizePos+1] = pstripe1[sizePos+1] ^ sw.i[1];
					}
					else
					{
						// Write the entire block, and put the file size at end
						sizeRecordRequired = true;
					}
				}

				// Write block
				if(::write(parity, parityBuffer, parityWriteSize) != parityWriteSize)
				{
					THROW_EXCEPTION(RaidFileException, OSError)
				}
			}

			// Write stripes
			char *writeFrom = buffer;
			for(int l = 0; l < blocksToDo; ++l)
			{
				// Write the block
				int toWrite = (l == (blocksToDo - 1))
								?(bytesRead - ((blocksToDo-1)*blockSize))
								:blockSize;
				if(::write(((l&1)==0)?stripe1:stripe2, writeFrom, toWrite) != toWrite)
				{
					THROW_EXCEPTION(RaidFileException, OSError)
				}			

				// Next block
				writeFrom += blockSize;
			}
			
			// Count of blocks done
			blocksDone += blocksToDo;
		}
		// Error on read?
		if(bytesRead == -1)
		{
			THROW_EXCEPTION(RaidFileException, OSError)
		}
		
		// Special case for zero length files
		if(writeFileStat.st_size == 0)
		{
			sizeRecordRequired = true;
		}

		// Might need to write the file size to the end of the parity file
		// if it can't be worked out some other means -- size is required to rebuild the file if one of the stripe files is missing
		if(sizeRecordRequired)
		{
			ASSERT(sizeof(writeFileStat.st_size) <= sizeof(RaidFileRead::FileSizeType));
			RaidFileRead::FileSizeType sw = box_hton64(writeFileStat.st_size);
			ASSERT((::lseek(parity, 0, SEEK_CUR) % blockSize) == 0);
			if(::write(parity, &sw, sizeof(sw)) != sizeof(sw))
			{
				THROW_EXCEPTION(RaidFileException, OSError)
			}
		}

		// Then close the written files (note in reverse order of opening)
		parity.Close();
		stripe2.Close();
		stripe1.Close();

#ifdef WIN32
		// Must delete before renaming
		::unlink(stripe1Filename.c_str());
		::unlink(stripe2Filename.c_str());
		::unlink(parityFilename.c_str());
#endif
		
		// Rename them into place
		if(::rename(stripe1FilenameW.c_str(), stripe1Filename.c_str()) != 0
			|| ::rename(stripe2FilenameW.c_str(), stripe2Filename.c_str()) != 0
			|| ::rename(parityFilenameW.c_str(), parityFilename.c_str()) != 0)
		{
			THROW_EXCEPTION(RaidFileException, OSError)
		}

		// Close the write file
		writeFile.Close();

		// Finally delete the write file
		if(::unlink(writeFilename.c_str()) != 0)
		{
			THROW_EXCEPTION(RaidFileException, OSError)
		}
	}
	catch(...)
	{
		// Unlink all the dodgy files
		::unlink(stripe1Filename.c_str());
		::unlink(stripe2Filename.c_str());
		::unlink(parityFilename.c_str());
		::unlink(stripe1FilenameW.c_str());
		::unlink(stripe2FilenameW.c_str());
		::unlink(parityFilenameW.c_str());
		
		// and send the error on its way
		throw;
	}
}



// --------------------------------------------------------------------------
//
// Function
//		Name:    RaidFileWrite::Delete()
//		Purpose: Deletes a RAID file
//		Created: 2003/07/13
//
// --------------------------------------------------------------------------
void RaidFileWrite::Delete()
{
	// Get disc set
	RaidFileController &rcontroller(RaidFileController::GetController());
	RaidFileDiscSet rdiscSet(rcontroller.GetDiscSet(mSetNumber));

	// See if the file exists already -- can't delete files which don't exist
	RaidFileUtil::ExistType existance = RaidFileUtil::RaidFileExists(rdiscSet, mFilename);
	if(existance == RaidFileUtil::NoFile)
	{
		THROW_EXCEPTION(RaidFileException, RaidFileDoesntExist)
	}

	// Get the filename for the write file
	std::string writeFilename(RaidFileUtil::MakeWriteFileName(rdiscSet, mFilename));

	// Attempt to delete it
	bool deletedSomething = false;
	if(::unlink(writeFilename.c_str()) == 0)
	{
		deletedSomething = true;
	}
	
	// If we're not running in RAID mode, stop now
	if(rdiscSet.size() == 1)
	{
		return;
	}
	
	// Now the other files
	std::string stripe1Filename(RaidFileUtil::MakeRaidComponentName(rdiscSet, mFilename, 0 % TRANSFORM_NUMBER_DISCS_REQUIRED));
	std::string stripe2Filename(RaidFileUtil::MakeRaidComponentName(rdiscSet, mFilename, 1 % TRANSFORM_NUMBER_DISCS_REQUIRED));
	std::string parityFilename(RaidFileUtil::MakeRaidComponentName(rdiscSet, mFilename, 2 % TRANSFORM_NUMBER_DISCS_REQUIRED));
	if(::unlink(stripe1Filename.c_str()) == 0)
	{
		deletedSomething = true;
	}
	if(::unlink(stripe2Filename.c_str()) == 0)
	{
		deletedSomething = true;
	}
	if(::unlink(parityFilename.c_str()) == 0)
	{
		deletedSomething = true;
	}
	
	// Check something happened
	if(!deletedSomething)
	{
		THROW_EXCEPTION(RaidFileException, OSError)
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    RaidFileWrite::CreateDirectory(int, const std::string &, bool, int)
//		Purpose: Creates a directory within the raid file directories with the given name.
//		Created: 2003/08/20
//
// --------------------------------------------------------------------------
void RaidFileWrite::CreateDirectory(int SetNumber, const std::string &rDirName, bool Recursive, int mode)
{
	// Get disc set
	RaidFileController &rcontroller(RaidFileController::GetController());
	RaidFileDiscSet rdiscSet(rcontroller.GetDiscSet(SetNumber));
	// Pass on...
	CreateDirectory(rdiscSet, rDirName, Recursive, mode);
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    RaidFileWrite::CreateDirectory(const RaidFileDiscSet &, const std::string &, bool, int)
//		Purpose: Creates a directory within the raid file directories with the given name.
//		Created: 2003/08/20
//
// --------------------------------------------------------------------------
void RaidFileWrite::CreateDirectory(const RaidFileDiscSet &rSet, const std::string &rDirName, bool Recursive, int mode)
{
	if(Recursive)
	{
		// split up string
		std::vector<std::string> elements;
		SplitString(rDirName, DIRECTORY_SEPARATOR_ASCHAR, elements);
		
		// Do each element in turn
		std::string pn;
		for(unsigned int e = 0; e < elements.size(); ++e)
		{
			// Only do this if the element has some text in it
			if(elements[e].size() > 0)
			{
				pn += elements[e];
				if(!RaidFileRead::DirectoryExists(rSet, pn))
				{
					CreateDirectory(rSet, pn, false, mode);
				}

				// add separator
				pn += DIRECTORY_SEPARATOR_ASCHAR;
			}
		}
	
		return;
	}
	
	// Create a directory in every disc of the set
	for(unsigned int l = 0; l < rSet.size(); ++l)
	{
		// build name
		std::string dn(rSet[l] + DIRECTORY_SEPARATOR + rDirName);
	
		// attempt to create
		if(::mkdir(dn.c_str(), mode) != 0)
		{
			if(errno == EEXIST)
			{
				// No. Bad things.
				THROW_EXCEPTION(RaidFileException, FileExistsInDirectoryCreation)
			}
			else
			{
				THROW_EXCEPTION(RaidFileException, OSError)
			}
		}
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    RaidFileWrite::Read(void *, int, int)
//		Purpose: Unsupported, will exception
//		Created: 2003/08/21
//
// --------------------------------------------------------------------------
int RaidFileWrite::Read(void *pBuffer, int NBytes, int Timeout)
{
	THROW_EXCEPTION(RaidFileException, UnsupportedReadWriteOrClose)
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    RaidFileWrite::Close()
//		Purpose: Close, discarding file.
//		Created: 2003/08/21
//
// --------------------------------------------------------------------------
void RaidFileWrite::Close()
{
	TRACE0("Warning: RaidFileWrite::Close() called, discarding file\n");
	if(mOSFileHandle != -1)
	{
		Discard();
	}
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    RaidFileWrite::StreamDataLeft()
//		Purpose: Never any data left to read!
//		Created: 2003/08/21
//
// --------------------------------------------------------------------------
bool RaidFileWrite::StreamDataLeft()
{
	return false;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    RaidFileWrite::StreamClosed()
//		Purpose: Is stream closed for writing?
//		Created: 2003/08/21
//
// --------------------------------------------------------------------------
bool RaidFileWrite::StreamClosed()
{
	return mOSFileHandle == -1;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    RaidFileWrite::GetFileSize()
//		Purpose: Returns the size of the file written.
//				 Can only be used before the file is commited.
//		Created: 2003/09/03
//
// --------------------------------------------------------------------------
IOStream::pos_type RaidFileWrite::GetFileSize()
{
	if(mOSFileHandle == -1)
	{
		THROW_EXCEPTION(RaidFileException, CanOnlyGetFileSizeBeforeCommit)
	}
	
	// Stat to get size
	struct stat st;
	if(fstat(mOSFileHandle, &st) != 0)
	{
		THROW_EXCEPTION(RaidFileException, OSError)
	}
	
	return st.st_size;
}




// --------------------------------------------------------------------------
//
// Function
//		Name:    RaidFileWrite::GetDiscUsageInBlocks()
//		Purpose: Returns the amount of disc space used, in blocks.
//				 Can only be used before the file is commited.
//		Created: 2003/09/03
//
// --------------------------------------------------------------------------
IOStream::pos_type RaidFileWrite::GetDiscUsageInBlocks()
{
	if(mOSFileHandle == -1)
	{
		THROW_EXCEPTION(RaidFileException, CanOnlyGetUsageBeforeCommit)
	}
	
	// Stat to get size
	struct stat st;
	if(fstat(mOSFileHandle, &st) != 0)
	{
		THROW_EXCEPTION(RaidFileException, OSError)
	}
	
	// Then return calculation
	RaidFileController &rcontroller(RaidFileController::GetController());
	RaidFileDiscSet rdiscSet(rcontroller.GetDiscSet(mSetNumber));
	return RaidFileUtil::DiscUsageInBlocks(st.st_size, rdiscSet);
}


