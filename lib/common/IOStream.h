// --------------------------------------------------------------------------
//
// File
//		Name:    IOStream.h
//		Purpose: I/O Stream abstraction
//		Created: 2003/07/31
//
// --------------------------------------------------------------------------

#ifndef IOSTREAM__H
#define IOSTREAM__H

// --------------------------------------------------------------------------
//
// Class
//		Name:    IOStream
//		Purpose: Abstract interface to streams of data
//		Created: 2003/07/31
//
// --------------------------------------------------------------------------
class IOStream
{
public:
	IOStream();
	IOStream(const IOStream &rToCopy);
	virtual ~IOStream();
	
	enum
	{
		TimeOutInfinite = -1,
		SizeOfStreamUnknown = -1
	};
	
	enum
	{
		SeekType_Absolute = 0,
		SeekType_Relative = 1,
		SeekType_End = 2
	};
	
	// Timeout in milliseconds
	// Read may return 0 -- does not mean end of stream.
	typedef int64_t pos_type;
	virtual int Read(void *pBuffer, int NBytes, int Timeout = IOStream::TimeOutInfinite) = 0;
	virtual pos_type BytesLeftToRead();	// may return IOStream::SizeOfStreamUnknown (and will for most stream types)
	virtual void Write(const void *pBuffer, int NBytes) = 0;
	virtual void WriteAllBuffered();
	virtual pos_type GetPosition() const;
	virtual void Seek(pos_type Offset, int SeekType);
	virtual void Close();
	
	// Has all data that can be read been read?
	virtual bool StreamDataLeft() = 0;
	// Has the stream been closed (writing not possible)
	virtual bool StreamClosed() = 0;
	
	// Utility functions
	bool ReadFullBuffer(void *pBuffer, int NBytes, int *pNBytesRead, int Timeout = IOStream::TimeOutInfinite);
	bool CopyStreamTo(IOStream &rCopyTo, int Timeout = IOStream::TimeOutInfinite, int BufferSize = 1024);
	void Flush(int Timeout = IOStream::TimeOutInfinite);
	
	static int ConvertSeekTypeToOSWhence(int SeekType);
};


#endif // IOSTREAM__H


