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
	virtual ~IOStream();

private:
	IOStream(const IOStream &rToCopy); /* forbidden */
	IOStream& operator=(const IOStream &rToCopy); /* forbidden */
		
public:
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
	virtual size_t Read(void *pBuffer, size_t NBytes, int Timeout = IOStream::TimeOutInfinite) = 0;
	virtual pos_type BytesLeftToRead();	// may return IOStream::SizeOfStreamUnknown (and will for most stream types)
	virtual void Write(const void *pBuffer, size_t NBytes) = 0;
	virtual void Write(const char *pBuffer);
	virtual void WriteAllBuffered();
	virtual pos_type GetPosition() const;
	virtual void Seek(pos_type Offset, int SeekType);
	virtual void Close();
	
	// Has all data that can be read been read?
	virtual bool StreamDataLeft() = 0;
	// Has the stream been closed (writing not possible)
	virtual bool StreamClosed() = 0;
	
	// Utility functions
	bool CopyStreamTo(IOStream &rCopyTo, int Timeout = IOStream::TimeOutInfinite, size_t BufferSize = 1024);
	void Flush(int Timeout = IOStream::TimeOutInfinite);
	
	static int ConvertSeekTypeToOSWhence(int SeekType);


	// things to make the compiler happier
	template <class T>
	inline T Read(void *pBuffer, T NBytes, int Timeout = IOStream::TimeOutInfinite)
	{
		if (NBytes >= SIZE_MAX)
			throw std::bad_cast();
		return static_cast<T>(Read(pBuffer,static_cast<size_t>(NBytes),Timeout));
	}
	template <class T>
	inline void Write(const void *pBuffer, T NBytes)
	{
		if (NBytes >= SIZE_MAX)
			throw std::bad_cast();
		Write(pBuffer,static_cast<size_t>(NBytes));
	}
	template <class T>
	inline bool ReadFullBuffer(void *pBuffer, T NBytes, void *pNBytesRead, int Timeout = IOStream::TimeOutInfinite)
	{
		if (NBytes >= SIZE_MAX)
			throw std::bad_cast();
		size_t n;
		bool b = mReadFullBuffer(pBuffer,static_cast<size_t>(NBytes),&n,Timeout);
		if(pNBytesRead)
			*static_cast<T*>(pNBytesRead) = static_cast<T>(n);
		return b;
	}

protected:
	bool mReadFullBuffer(void *pBuffer, size_t NBytes, size_t *pNBytesRead, int Timeout = IOStream::TimeOutInfinite);
};


#endif // IOSTREAM__H


