// --------------------------------------------------------------------------
//
// File
//		Name:    CompressStream.h
//		Purpose: Compressing stream
//		Created: 27/5/04
//
// --------------------------------------------------------------------------

#ifndef COMPRESSSTREAM__H
#define COMPRESSSTREAM__H

#include "IOStream.h"

// --------------------------------------------------------------------------
//
// Class
//		Name:    CompressStream
//		Purpose: Compressing stream
//		Created: 27/5/04
//
// --------------------------------------------------------------------------
class CompressStream : public IOStream
{
public:
	CompressStream(IOStream *pStream, bool TakeOwnership,
		bool DecompressRead, bool CompressWrite, bool PassThroughWhenNotCompressed = false);
	~CompressStream();
private:
	// No copying (have implementations which exception)
	CompressStream(const CompressStream &);
	CompressStream &operator=(const CompressStream &);
public:

	virtual int Read(void *pBuffer, int NBytes, int Timeout = IOStream::TimeOutInfinite);
	virtual void Write(const void *pBuffer, int NBytes);
	virtual void WriteAllBuffered();
	virtual void Close();
	virtual bool StreamDataLeft();
	virtual bool StreamClosed();

protected:
	void CheckRead();
	void CheckWrite();
	void CheckBuffer();
	void WriteCompressedData(bool SyncFlush = false);

private:
	IOStream *mpStream;
	bool mHaveOwnership;
	bool mDecompressRead;
	bool mCompressWrite;
	bool mPassThroughWhenNotCompressed;
	// Avoid having to include Compress.h
	void *mpReadCompressor;
	void *mpWriteCompressor;
	void *mpBuffer;
	bool mIsClosed;
};

#endif // COMPRESSSTREAM__H

