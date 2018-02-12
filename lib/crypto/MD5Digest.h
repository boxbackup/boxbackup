// --------------------------------------------------------------------------
//
// File
//		Name:    MD5Digest.h
//		Purpose: Simple interface for creating MD5 digests
//		Created: 8/12/03
//
// --------------------------------------------------------------------------

#ifndef MD5DIGEST_H
#define MD5DIGEST_H

#include <openssl/md5.h>
#include <string>

#include "Exception.h"
#include "IOStream.h"

// --------------------------------------------------------------------------
//
// Function
//		Name:    MD5Digest
//		Purpose: Simple interface for creating MD5 digests
//		Created: 8/12/03
//
// --------------------------------------------------------------------------
class MD5Digest
{
public:
	MD5Digest();
	virtual ~MD5Digest();

	void Add(const std::string &rString);
	void Add(const void *pData, int Length);

	void Finish();

	std::string DigestAsString();
	uint8_t *DigestAsData(int *pLength = 0)
	{
		if(pLength) *pLength = sizeof(mDigest);
		return mDigest;
	}

	enum
	{
		DigestLength = MD5_DIGEST_LENGTH
	};

	int CopyDigestTo(uint8_t *to);
	bool DigestMatches(uint8_t *pCompareWith) const;

private:
	MD5_CTX md5;
	uint8_t mDigest[MD5_DIGEST_LENGTH];
};

class MD5DigestStream : public IOStream
{
private:
	MD5Digest mDigest;
	MD5DigestStream(const MD5DigestStream &rToCopy); /* forbidden */
	MD5DigestStream& operator=(const MD5DigestStream &rToCopy); /* forbidden */
	bool mClosed;

public:
	MD5DigestStream()
	: mClosed(false)
	{ }

	virtual int Read(void *pBuffer, int NBytes,
		int Timeout = IOStream::TimeOutInfinite)
	{
		THROW_EXCEPTION(CommonException, NotSupported);
	}
	virtual pos_type BytesLeftToRead()
	{
		THROW_EXCEPTION(CommonException, NotSupported);
	}
	virtual void Write(const void *pBuffer, int NBytes,
		int Timeout = IOStream::TimeOutInfinite)
	{
		mDigest.Add(pBuffer, NBytes);
	}
	virtual void Write(const std::string& rBuffer,
		int Timeout = IOStream::TimeOutInfinite)
	{
		mDigest.Add(rBuffer);
	}
	virtual void WriteAllBuffered(int Timeout = IOStream::TimeOutInfinite) { }
	virtual pos_type GetPosition() const
	{
		THROW_EXCEPTION(CommonException, NotSupported);
	}
	virtual void Seek(pos_type Offset, int SeekType)
	{
		THROW_EXCEPTION(CommonException, NotSupported);
	}
	virtual void Close()
	{
		mDigest.Finish();
		mClosed = true;
	}

	// Has all data that can be read been read?
	virtual bool StreamDataLeft()
	{
		THROW_EXCEPTION(CommonException, NotSupported);
	}
	// Has the stream been closed (writing not possible)
	virtual bool StreamClosed()
	{
		return mClosed;
	}

	// Utility functions
	bool ReadFullBuffer(void *pBuffer, int NBytes, int *pNBytesRead,
		int Timeout = IOStream::TimeOutInfinite)
	{
		THROW_EXCEPTION(CommonException, NotSupported);
	}
	IOStream::pos_type CopyStreamTo(IOStream &rCopyTo,
		int Timeout = IOStream::TimeOutInfinite, int BufferSize = 1024)
	{
		THROW_EXCEPTION(CommonException, NotSupported);
	}
	void Flush(int Timeout = IOStream::TimeOutInfinite)
	{ }
	static int ConvertSeekTypeToOSWhence(int SeekType)
	{
		THROW_EXCEPTION(CommonException, NotSupported);
	}
	virtual std::string ToString() const
	{
		return "MD5DigestStream";
	}

	std::string DigestAsString()
	{
		// You can only get a digest when the stream has been closed, because this
		// finalises the underlying MD5Digest object.
		ASSERT(mClosed);
		return mDigest.DigestAsString();
	}
	uint8_t *DigestAsData(int *pLength = 0)
	{
		// You can only get a digest when the stream has been closed, because this
		// finalises the underlying MD5Digest object.
		ASSERT(mClosed);
		return mDigest.DigestAsData(pLength);
	}
	int CopyDigestTo(uint8_t *to)
	{
		// You can only get a digest when the stream has been closed, because this
		// finalises the underlying MD5Digest object.
		ASSERT(mClosed);
		return mDigest.CopyDigestTo(to);
	}
	bool DigestMatches(uint8_t *pCompareWith) const
	{
		// You can only get a digest when the stream has been closed, because this
		// finalises the underlying MD5Digest object.
		ASSERT(mClosed);
		return mDigest.DigestMatches(pCompareWith);
	}
};

#endif // MD5DIGEST_H

