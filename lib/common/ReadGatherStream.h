// --------------------------------------------------------------------------
//
// File
//		Name:    ReadGatherStream.h
//		Purpose: Build a stream (for reading only) out of a number of other streams.
//		Created: 10/12/03
//
// --------------------------------------------------------------------------

#ifndef READGATHERSTREAM_H
#define READGATHERSTREAM_H

#include "IOStream.h"

#include <vector>

// --------------------------------------------------------------------------
//
// Class
//		Name:    ReadGatherStream
//		Purpose: Build a stream (for reading only) out of a number of other streams.
//		Created: 10/12/03
//
// --------------------------------------------------------------------------
class ReadGatherStream : public IOStream
{
public:
	ReadGatherStream(bool DeleteComponentStreamsOnDestruction);
	~ReadGatherStream();
private:
	ReadGatherStream(const ReadGatherStream &);
	ReadGatherStream &operator=(const ReadGatherStream &);
public:

	size_t AddComponent(IOStream *pStream);
	void AddBlock(int Component, pos_type Length, bool Seek = false, pos_type SeekTo = 0);

	virtual size_t Read(void *pBuffer, size_t NBytes, int Timeout = IOStream::TimeOutInfinite);
	virtual pos_type BytesLeftToRead();
	virtual void Write(const void *pBuffer, int NBytes);
	virtual bool StreamDataLeft();
	virtual bool StreamClosed();
	virtual pos_type GetPosition() const;

private:
	bool mDeleteComponentStreamsOnDestruction;
	std::vector<IOStream *> mComponents;
	
	typedef struct
	{
		pos_type mLength;
		pos_type mSeekTo;
		int mComponent;
		bool mSeek;
	} Block;
	
	std::vector<Block> mBlocks;
	
	pos_type mCurrentPosition;
	pos_type mTotalSize;
	unsigned int mCurrentBlock;
	pos_type mPositionInCurrentBlock;
	bool mSeekDoneForCurrent;
};


#endif // READGATHERSTREAM_H
