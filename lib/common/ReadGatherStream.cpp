// --------------------------------------------------------------------------
//
// File
//		Name:    ReadGatherStream.cpp
//		Purpose: Build a stream (for reading only) out of a number of other streams.
//		Created: 10/12/03
//
// --------------------------------------------------------------------------

#include "Box.h"

#include "ReadGatherStream.h"
#include "CommonException.h"

#include "MemLeakFindOn.h"

// --------------------------------------------------------------------------
//
// Function
//		Name:    ReadGatherStream::ReadGatherStream(bool)
//		Purpose: Constructor. Args says whether or not all the component streams will be deleted when this
//				 object is deleted.
//		Created: 10/12/03
//
// --------------------------------------------------------------------------
ReadGatherStream::ReadGatherStream(bool DeleteComponentStreamsOnDestruction)
	: mDeleteComponentStreamsOnDestruction(DeleteComponentStreamsOnDestruction),
	  mCurrentPosition(0),
	  mTotalSize(0),
	  mCurrentBlock(0),
	  mPositionInCurrentBlock(0),
	  mSeekDoneForCurrent(false)
{
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    ReadGatherStream::~ReadGatherStream()
//		Purpose: Destructor. Will delete all the stream objects, if required.
//		Created: 10/12/03
//
// --------------------------------------------------------------------------
ReadGatherStream::~ReadGatherStream()
{
	// Delete compoenent streams?
	if(mDeleteComponentStreamsOnDestruction)
	{
		for(unsigned int l = 0; l < mComponents.size(); ++l)
		{
			delete mComponents[l];
		}
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    ReadGatherStream::AddComponent(IOStream *)
//		Purpose: Add a component to this stream, returning the index of this component
//				 in the internal list. Use this with AddBlock()
//		Created: 10/12/03
//
// --------------------------------------------------------------------------
int ReadGatherStream::AddComponent(IOStream *pStream)
{
	ASSERT(pStream != 0);

	// Just add the component to the list, returning it's index.
	int index = mComponents.size();
	mComponents.push_back(pStream);
	return index;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    ReadGatherStream::AddBlock(int, pos_type, bool, pos_type)
//		Purpose: Add a block to the list of blocks being gathered into one stream.
//				 Length is length of block to read from this component, Seek == true
//				 if a seek is required, and if true, SeekTo is the position (absolute)
//				 in the stream to be seeked to when this block is required.
//		Created: 10/12/03
//
// --------------------------------------------------------------------------
void ReadGatherStream::AddBlock(int Component, pos_type Length, bool Seek, pos_type SeekTo)
{
	// Check block
	if(Component < 0 || Component >= (int)mComponents.size() || Length < 0 || SeekTo < 0)
	{
		THROW_EXCEPTION(CommonException, ReadGatherStreamAddingBadBlock);
	}
	
	// Add to list
	Block b;
	b.mLength = Length;
	b.mSeekTo = SeekTo;
	b.mComponent = Component;
	b.mSeek = Seek;
	
	mBlocks.push_back(b);
	
	// And update the total size
	mTotalSize += Length;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    ReadGatherStream::Read(void *, int, int)
//		Purpose: As interface.
//		Created: 10/12/03
//
// --------------------------------------------------------------------------
int ReadGatherStream::Read(void *pBuffer, int NBytes, int Timeout)
{
	int bytesToRead = NBytes;
	uint8_t *buffer = (uint8_t*)pBuffer;
	
	while(bytesToRead > 0)
	{
		// Done?
		if(mCurrentBlock >= mBlocks.size())
		{
			// Stop now, as have finished the last block
			return NBytes - bytesToRead;
		}
			
		// Seek?
		if(mPositionInCurrentBlock == 0 && mBlocks[mCurrentBlock].mSeek && !mSeekDoneForCurrent)
		{
			// Do seeks in this manner so that seeks are done regardless of whether the block
			// has length > 0, and it will only be done once, and at as late a stage as possible.
			
			mComponents[mBlocks[mCurrentBlock].mComponent]->Seek(mBlocks[mCurrentBlock].mSeekTo, IOStream::SeekType_Absolute);
		
			mSeekDoneForCurrent = true;
		}

		// Anything in the current block?
		if(mPositionInCurrentBlock < mBlocks[mCurrentBlock].mLength)
		{
			// Read!
			int s = mBlocks[mCurrentBlock].mLength - mPositionInCurrentBlock;
			if(s > bytesToRead) s = bytesToRead;
			
			int r = mComponents[mBlocks[mCurrentBlock].mComponent]->Read(buffer, s, Timeout);
			
			// update variables
			mPositionInCurrentBlock += r;
			buffer += r;
			bytesToRead -= r;
			mCurrentPosition += r;
			
			if(r != s)
			{
				// Stream returned less than requested. To avoid blocking when not necessary,
				// return now.
				return NBytes - bytesToRead;
			}
		}
		else
		{
			// Move to next block
			++mCurrentBlock;
			mPositionInCurrentBlock = 0;
			mSeekDoneForCurrent = false;
		}
	}

	return NBytes - bytesToRead;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    ReadGatherStream::GetPosition()
//		Purpose: As interface
//		Created: 10/12/03
//
// --------------------------------------------------------------------------
IOStream::pos_type ReadGatherStream::GetPosition() const
{
	return mCurrentPosition;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    ReadGatherStream::BytesLeftToRead()
//		Purpose: As interface
//		Created: 10/12/03
//
// --------------------------------------------------------------------------
IOStream::pos_type ReadGatherStream::BytesLeftToRead()
{
	return mTotalSize - mCurrentPosition;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    ReadGatherStream::Write(const void *, int)
//		Purpose: As interface.
//		Created: 10/12/03
//
// --------------------------------------------------------------------------
void ReadGatherStream::Write(const void *pBuffer, int NBytes)
{
	THROW_EXCEPTION(CommonException, CannotWriteToReadGatherStream);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    ReadGatherStream::StreamDataLeft()
//		Purpose: As interface.
//		Created: 10/12/03
//
// --------------------------------------------------------------------------
bool ReadGatherStream::StreamDataLeft()
{
	if(mCurrentBlock >= mBlocks.size())
	{
		// Done all the blocks
		return false;
	}
	
	if(mCurrentBlock == (mBlocks.size() - 1)
		&& mPositionInCurrentBlock >= mBlocks[mCurrentBlock].mLength)
	{
		// Are on the last block, and have got all the data from it.
		return false;
	}

	// Otherwise, there's more data to be read
	return true;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    ReadGatherStream::StreamClosed()
//		Purpose: As interface. But the stream is always closed.
//		Created: 10/12/03
//
// --------------------------------------------------------------------------
bool ReadGatherStream::StreamClosed()
{
	return true;
}


