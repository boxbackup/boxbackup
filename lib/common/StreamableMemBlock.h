// --------------------------------------------------------------------------
//
// File
//		Name:    StreamableMemBlock.h
//		Purpose: Memory blocks which can be loaded and saved from streams,
//		         with a header indicating the size of the block.
//		Created: 2003/09/05
//
// --------------------------------------------------------------------------

#ifndef STREAMABLEMEMBLOCK__H
#define STREAMABLEMEMBLOCK__H

class IOStream;

// --------------------------------------------------------------------------
//
// Class
//		Name:    StreamableMemBlock
//		Purpose: Memory blocks which can be loaded and saved from streams
//		Created: 2003/09/05
//
// --------------------------------------------------------------------------
class StreamableMemBlock
{
public:
	StreamableMemBlock();
	StreamableMemBlock(int Size);
	StreamableMemBlock(const void *pBuffer, int Size);
	StreamableMemBlock(const StreamableMemBlock &rToCopy);
	~StreamableMemBlock();
	
	void Set(const StreamableMemBlock &rBlock);
	void Set(const void *pBuffer, int Size);
	void Set(IOStream &rStream, int Timeout);
	StreamableMemBlock &operator=(const StreamableMemBlock &rBlock)
	{
		Set(rBlock);
		return *this;
	}

	void ReadFromStream(IOStream &rStream, int Timeout);
	void WriteToStream(IOStream &rStream) const;
	
	static void WriteEmptyBlockToStream(IOStream &rStream);
	
	void *GetBuffer() const;
	
	// Size of block
	int GetSize() const {return mSize;}

	// Buffer empty?
	bool IsEmpty() const {return mSize == 0;}

	// Clear the contents of the block
	void Clear() {FreeBlock();}
	
	bool operator==(const StreamableMemBlock &rCompare) const;

	void ResizeBlock(int Size);

protected:	// be careful with these!
	void AllocateBlock(int Size);
	void FreeBlock();

private:
	void *mpBuffer;
	int mSize;
};

#endif // STREAMABLEMEMBLOCK__H

