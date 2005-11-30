// --------------------------------------------------------------------------
//
// File
//		Name:    Protocol.cpp
//		Purpose: Generic protocol support
//		Created: 2003/08/19
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <sys/types.h>

#include <stdlib.h>
#include <string.h>

#include <new>

#include "Protocol.h"
#include "ProtocolWire.h"
#include "IOStream.h"
#include "ServerException.h"
#include "PartialReadStream.h"
#include "ProtocolUncertainStream.h"

#include "MemLeakFindOn.h"

#ifdef NDEBUG
	#define PROTOCOL_ALLOCATE_SEND_BLOCK_CHUNK	1024
#else
//	#define PROTOCOL_ALLOCATE_SEND_BLOCK_CHUNK	1024
	#define PROTOCOL_ALLOCATE_SEND_BLOCK_CHUNK	4
#endif

#define UNCERTAIN_STREAM_SIZE_BLOCK	(64*1024)

// --------------------------------------------------------------------------
//
// Function
//		Name:    Protocol::Protocol(IOStream &rStream)
//		Purpose: Constructor
//		Created: 2003/08/19
//
// --------------------------------------------------------------------------
Protocol::Protocol(IOStream &rStream)
	: mrStream(rStream),
	  mHandshakeDone(false),
	  mMaxObjectSize(PROTOCOL_DEFAULT_MAXOBJSIZE),
	  mTimeout(PROTOCOL_DEFAULT_TIMEOUT),
	  mpBuffer(0),
	  mBufferSize(0),
	  mReadOffset(-1),
	  mWriteOffset(-1),
	  mValidDataSize(-1),
	  mLastErrorType(NoError),
	  mLastErrorSubType(NoError)
{
	TRACE1("Send block allocation size is %d\n", PROTOCOL_ALLOCATE_SEND_BLOCK_CHUNK);
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    Protocol::~Protocol()
//		Purpose: Destructor
//		Created: 2003/08/19
//
// --------------------------------------------------------------------------
Protocol::~Protocol()
{
	// Free buffer?
	if(mpBuffer != 0)
	{
		free(mpBuffer);
		mpBuffer = 0;
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    Protocol::GetLastError(int &, int &)
//		Purpose: Returns true if there was an error, and type and subtype if there was.
//		Created: 2003/08/19
//
// --------------------------------------------------------------------------
bool Protocol::GetLastError(int &rTypeOut, int &rSubTypeOut)
{
	if(mLastErrorType == NoError)
	{
		// no error.
		return false;
	}
	
	// Return type and subtype in args
	rTypeOut = mLastErrorType;
	rSubTypeOut = mLastErrorSubType;
	
	// and unset them
	mLastErrorType = NoError;
	mLastErrorSubType = NoError;
	
	return true;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    Protocol::Handshake()
//		Purpose: Handshake with peer (exchange ident strings)
//		Created: 2003/08/20
//
// --------------------------------------------------------------------------
void Protocol::Handshake()
{
	// Already done?
	if(mHandshakeDone)
	{
		THROW_EXCEPTION(CommonException, Internal)
	}

	// Make handshake block
	PW_Handshake hsSend;
	::memset(&hsSend, 0, sizeof(hsSend));
	// Copy in ident string
	::strncpy(hsSend.mIdent, GetIdentString(), sizeof(hsSend.mIdent));
	
	// Send it
	mrStream.Write(&hsSend, sizeof(hsSend));
	mrStream.WriteAllBuffered();

	// Receive a handshake from the peer
	PW_Handshake hsReceive;
	::memset(&hsReceive, 0, sizeof(hsReceive));
	char *readInto = (char*)&hsReceive;
	int bytesToRead = sizeof(hsReceive);
	while(bytesToRead > 0)
	{
		// Get some data from the stream
		int bytesRead = mrStream.Read(readInto, bytesToRead, mTimeout);
		if(bytesRead == 0)
		{
			THROW_EXCEPTION(ConnectionException, Conn_Protocol_Timeout)
		}
		readInto += bytesRead;
		bytesToRead -= bytesRead;
	}
	ASSERT(bytesToRead == 0);
	
	// Are they the same?
	if(::memcmp(&hsSend, &hsReceive, sizeof(hsSend)) != 0)
	{
		THROW_EXCEPTION(ConnectionException, Conn_Protocol_HandshakeFailed)
	}

	// Mark as done
	mHandshakeDone = true;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    Protocol::CheckAndReadHdr(void *)
//		Purpose: Check read for recieve call and get object header from stream.
//				 Don't use type here to avoid dependency in .h file.
//		Created: 2003/08/26
//
// --------------------------------------------------------------------------
void Protocol::CheckAndReadHdr(void *hdr)
{
	// Check usage
	if(mValidDataSize != -1 || mWriteOffset != -1 || mReadOffset != -1)
	{
		THROW_EXCEPTION(ServerException, Protocol_BadUsage)
	}
	
	// Handshake done?
	if(!mHandshakeDone)
	{
		Handshake();
	}

	// Get some data into this header
	if(!mrStream.ReadFullBuffer(hdr, sizeof(PW_ObjectHeader), 0 /* not interested in bytes read if this fails */, mTimeout))
	{
		THROW_EXCEPTION(ConnectionException, Conn_Protocol_Timeout)
	}
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    Protocol::Recieve()
//		Purpose: Recieves an object from the stream, creating it from the factory object type
//		Created: 2003/08/19
//
// --------------------------------------------------------------------------
std::auto_ptr<ProtocolObject> Protocol::Receive()
{
	// Get object header
	PW_ObjectHeader objHeader;
	CheckAndReadHdr(&objHeader);
	
	// Hope it's not a stream
	if(ntohl(objHeader.mObjType) == SPECIAL_STREAM_OBJECT_TYPE)
	{
		THROW_EXCEPTION(ConnectionException, Conn_Protocol_StreamWhenObjExpected)
	}
	
	// Check the object size
	u_int32_t objSize = ntohl(objHeader.mObjSize);
	if(objSize < sizeof(objHeader) || objSize > mMaxObjectSize)
	{
		THROW_EXCEPTION(ConnectionException, Conn_Protocol_ObjTooBig)
	}

	// Create a blank object
	std::auto_ptr<ProtocolObject> obj(MakeProtocolObject(ntohl(objHeader.mObjType)));

	// Make sure memory is allocated to read it into
	EnsureBufferAllocated(objSize);
	
	// Read data
	if(!mrStream.ReadFullBuffer(mpBuffer, objSize - sizeof(objHeader), 0 /* not interested in bytes read if this fails */, mTimeout))
	{
		THROW_EXCEPTION(ConnectionException, Conn_Protocol_Timeout)
	}

	// Setup ready to read out data from the buffer
	mValidDataSize = objSize - sizeof(objHeader);
	mReadOffset = 0;

	// Get the object to read its properties from the data recieved
	try
	{
		obj->SetPropertiesFromStreamData(*this);
	}
	catch(...)
	{
		// Make sure state is reset!
		mValidDataSize = -1;
		mReadOffset = -1;
		throw;
	}
	
	// Any data left over?
	bool dataLeftOver = (mValidDataSize != mReadOffset);
	
	// Unset read state, so future read calls don't fail
	mValidDataSize = -1;
	mReadOffset = -1;
	
	// Exception if not all the data was consumed
	if(dataLeftOver)
	{
		THROW_EXCEPTION(ConnectionException, Conn_Protocol_BadCommandRecieved)
	}

	return obj;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    Protocol::Send()
//		Purpose: Send an object to the other side of the connection.
//		Created: 2003/08/19
//
// --------------------------------------------------------------------------
void Protocol::Send(const ProtocolObject &rObject)
{
	// Check usage
	if(mValidDataSize != -1 || mWriteOffset != -1 || mReadOffset != -1)
	{
		THROW_EXCEPTION(ServerException, Protocol_BadUsage)
	}

	// Handshake done?
	if(!mHandshakeDone)
	{
		Handshake();
	}

	// Make sure there's a little bit of space allocated
	EnsureBufferAllocated(((sizeof(PW_ObjectHeader) + PROTOCOL_ALLOCATE_SEND_BLOCK_CHUNK - 1) / PROTOCOL_ALLOCATE_SEND_BLOCK_CHUNK) * PROTOCOL_ALLOCATE_SEND_BLOCK_CHUNK);
	ASSERT(mBufferSize >= (int)sizeof(PW_ObjectHeader));
	
	// Setup for write operation
	mValidDataSize = 0;		// Not used, but must not be -1
	mWriteOffset = sizeof(PW_ObjectHeader);
	
	try
	{
		rObject.WritePropertiesToStreamData(*this);
	}
	catch(...)
	{
		// Make sure state is reset!
		mValidDataSize = -1;
		mWriteOffset = -1;
		throw;
	}

	// How big?
	int writtenSize = mWriteOffset;

	// Reset write state
	mValidDataSize = -1;
	mWriteOffset = -1;	
	
	// Make header in the existing block
	PW_ObjectHeader *pobjHeader = (PW_ObjectHeader*)(mpBuffer);
	pobjHeader->mObjSize = htonl(writtenSize);
	pobjHeader->mObjType = htonl(rObject.GetType());

	// Write data
	mrStream.Write(mpBuffer, writtenSize);
	mrStream.WriteAllBuffered();
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    Protocol::EnsureBufferAllocated(int)
//		Purpose: Private. Ensures the buffer is at least the size requested.
//		Created: 2003/08/19
//
// --------------------------------------------------------------------------
void Protocol::EnsureBufferAllocated(int Size)
{
	if(mpBuffer != 0 && mBufferSize >= Size)
	{
		// Nothing to do!
		return;
	}
	
	// Need to allocate, or reallocate, the block
	if(mpBuffer != 0)
	{
		// Reallocate
		void *b = realloc(mpBuffer, Size);
		if(b == 0)
		{
			throw std::bad_alloc();
		}
		mpBuffer = (char*)b;
		mBufferSize = Size;
	}
	else
	{
		// Just allocate
		mpBuffer = (char*)malloc(Size);
		if(mpBuffer == 0)
		{
			throw std::bad_alloc();
		}
		mBufferSize = Size;
	}
}


#define READ_START_CHECK														\
	if(mValidDataSize == -1 || mWriteOffset != -1 || mReadOffset == -1)			\
	{																			\
		THROW_EXCEPTION(ServerException, Protocol_BadUsage)						\
	}

#define READ_CHECK_BYTES_AVAILABLE(bytesRequired)								\
	if((mReadOffset + (int)(bytesRequired)) > mValidDataSize)					\
	{																			\
		THROW_EXCEPTION(ConnectionException, Conn_Protocol_BadCommandRecieved)	\
	}

// --------------------------------------------------------------------------
//
// Function
//		Name:    Protocol::Read(void *, int)
//		Purpose: Read raw data from the stream (buffered)
//		Created: 2003/08/19
//
// --------------------------------------------------------------------------
void Protocol::Read(void *Buffer, int Size)
{
	READ_START_CHECK
	READ_CHECK_BYTES_AVAILABLE(Size)
	
	// Copy data out
	::memmove(Buffer, mpBuffer + mReadOffset, Size);
	mReadOffset += Size;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    Protocol::Read(std::string &, int)
//		Purpose: Read raw data from the stream (buffered), into a std::string
//		Created: 2003/08/26
//
// --------------------------------------------------------------------------
void Protocol::Read(std::string &rOut, int Size)
{
	READ_START_CHECK
	READ_CHECK_BYTES_AVAILABLE(Size)
	
	rOut.assign(mpBuffer + mReadOffset, Size);
	mReadOffset += Size;
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    Protocol::Read(int64_t &)
//		Purpose: Read a value from the stream (buffered)
//		Created: 2003/08/19
//
// --------------------------------------------------------------------------
void Protocol::Read(int64_t &rOut)
{
	READ_START_CHECK
	READ_CHECK_BYTES_AVAILABLE(sizeof(int64_t))
	
#ifdef PLATFORM_ALIGN_INT
	int64_t nvalue;
	memcpy(&nvalue, mpBuffer + mReadOffset, sizeof(int64_t));
#else
	int64_t nvalue = *((int64_t*)(mpBuffer + mReadOffset));
#endif
	rOut = ntoh64(nvalue);

	mReadOffset += sizeof(int64_t);
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    Protocol::Read(int32_t &)
//		Purpose: Read a value from the stream (buffered)
//		Created: 2003/08/19
//
// --------------------------------------------------------------------------
void Protocol::Read(int32_t &rOut)
{
	READ_START_CHECK
	READ_CHECK_BYTES_AVAILABLE(sizeof(int32_t))
	
#ifdef PLATFORM_ALIGN_INT
	int32_t nvalue;
	memcpy(&nvalue, mpBuffer + mReadOffset, sizeof(int32_t));
#else
	int32_t nvalue = *((int32_t*)(mpBuffer + mReadOffset));
#endif
	rOut = ntohl(nvalue);
	mReadOffset += sizeof(int32_t);
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    Protocol::Read(int16_t &)
//		Purpose: Read a value from the stream (buffered)
//		Created: 2003/08/19
//
// --------------------------------------------------------------------------
void Protocol::Read(int16_t &rOut)
{
	READ_START_CHECK
	READ_CHECK_BYTES_AVAILABLE(sizeof(int16_t))

	rOut = ntohs(*((int16_t*)(mpBuffer + mReadOffset)));
	mReadOffset += sizeof(int16_t);
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    Protocol::Read(int8_t &)
//		Purpose: Read a value from the stream (buffered)
//		Created: 2003/08/19
//
// --------------------------------------------------------------------------
void Protocol::Read(int8_t &rOut)
{
	READ_START_CHECK
	READ_CHECK_BYTES_AVAILABLE(sizeof(int8_t))

	rOut = *((int8_t*)(mpBuffer + mReadOffset));
	mReadOffset += sizeof(int8_t);
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    Protocol::Read(std::string &)
//		Purpose: Read a value from the stream (buffered)
//		Created: 2003/08/19
//
// --------------------------------------------------------------------------
void Protocol::Read(std::string &rOut)
{
	// READ_START_CHECK implied
	int32_t size;
	Read(size);
	
	READ_CHECK_BYTES_AVAILABLE(size)
	
	// initialise string
	rOut.assign(mpBuffer + mReadOffset, size);
	mReadOffset += size;
}




#define WRITE_START_CHECK														\
	if(mValidDataSize == -1 || mWriteOffset == -1 || mReadOffset != -1)			\
	{																			\
		THROW_EXCEPTION(ServerException, Protocol_BadUsage)						\
	}

#define WRITE_ENSURE_BYTES_AVAILABLE(bytesToWrite)								\
	if(mWriteOffset + (int)(bytesToWrite) > mBufferSize)						\
	{																			\
		EnsureBufferAllocated((((mWriteOffset + (int)(bytesToWrite)) + PROTOCOL_ALLOCATE_SEND_BLOCK_CHUNK - 1) / PROTOCOL_ALLOCATE_SEND_BLOCK_CHUNK) * PROTOCOL_ALLOCATE_SEND_BLOCK_CHUNK); \
		ASSERT(mWriteOffset + (int)(bytesToWrite) <= mBufferSize);			\
	}

// --------------------------------------------------------------------------
//
// Function
//		Name:    Protocol::Write(const void *, int)
//		Purpose: Writes the contents of a buffer to the stream
//		Created: 2003/08/19
//
// --------------------------------------------------------------------------
void Protocol::Write(const void *Buffer, int Size)
{
	WRITE_START_CHECK
	WRITE_ENSURE_BYTES_AVAILABLE(Size)
	
	::memmove(mpBuffer + mWriteOffset, Buffer, Size);
	mWriteOffset += Size;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    Protocol::Write(int64_t)
//		Purpose: Writes a value to the stream
//		Created: 2003/08/19
//
// --------------------------------------------------------------------------
void Protocol::Write(int64_t Value)
{
	WRITE_START_CHECK
	WRITE_ENSURE_BYTES_AVAILABLE(sizeof(int64_t))

	int64_t nvalue = hton64(Value);
#ifdef PLATFORM_ALIGN_INT
	memcpy(mpBuffer + mWriteOffset, &nvalue, sizeof(int64_t));
#else
	*((int64_t*)(mpBuffer + mWriteOffset)) = nvalue;
#endif
	mWriteOffset += sizeof(int64_t);
}


// --------------------------------------------------------------------------
//
// Function
//		Name:    Protocol::Write(int32_t)
//		Purpose: Writes a value to the stream
//		Created: 2003/08/19
//
// --------------------------------------------------------------------------
void Protocol::Write(int32_t Value)
{
	WRITE_START_CHECK
	WRITE_ENSURE_BYTES_AVAILABLE(sizeof(int32_t))

	int32_t nvalue = htonl(Value);
#ifdef PLATFORM_ALIGN_INT
	memcpy(mpBuffer + mWriteOffset, &nvalue, sizeof(int32_t));
#else
	*((int32_t*)(mpBuffer + mWriteOffset)) = nvalue;
#endif
	mWriteOffset += sizeof(int32_t);
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    Protocol::Write(int16_t)
//		Purpose: Writes a value to the stream
//		Created: 2003/08/19
//
// --------------------------------------------------------------------------
void Protocol::Write(int16_t Value)
{
	WRITE_START_CHECK
	WRITE_ENSURE_BYTES_AVAILABLE(sizeof(int16_t))

	*((int16_t*)(mpBuffer + mWriteOffset)) = htons(Value);
	mWriteOffset += sizeof(int16_t);
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    Protocol::Write(int8_t)
//		Purpose: Writes a value to the stream
//		Created: 2003/08/19
//
// --------------------------------------------------------------------------
void Protocol::Write(int8_t Value)
{
	WRITE_START_CHECK
	WRITE_ENSURE_BYTES_AVAILABLE(sizeof(int8_t))

	*((int8_t*)(mpBuffer + mWriteOffset)) = Value;
	mWriteOffset += sizeof(int8_t);
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    Protocol::Write(const std::string &)
//		Purpose: Writes a value to the stream
//		Created: 2003/08/19
//
// --------------------------------------------------------------------------
void Protocol::Write(const std::string &rValue)
{
	// WRITE_START_CHECK implied
	Write((int32_t)(rValue.size()));
	
	WRITE_ENSURE_BYTES_AVAILABLE(rValue.size())
	Write(rValue.c_str(), rValue.size());
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    Protocol::ReceieveStream()
//		Purpose: Receive a stream from the remote side
//		Created: 2003/08/26
//
// --------------------------------------------------------------------------
std::auto_ptr<IOStream> Protocol::ReceiveStream()
{
	// Get object header
	PW_ObjectHeader objHeader;
	CheckAndReadHdr(&objHeader);
	
	// Hope it's not an object
	if(ntohl(objHeader.mObjType) != SPECIAL_STREAM_OBJECT_TYPE)
	{
		THROW_EXCEPTION(ConnectionException, Conn_Protocol_ObjWhenStreamExpected)
	}
	
	// Get the stream size
	u_int32_t streamSize = ntohl(objHeader.mObjSize);

	// Inform sub class
	InformStreamReceiving(streamSize);

	// Return a stream object
	return std::auto_ptr<IOStream>((streamSize == ProtocolStream_SizeUncertain)?
		((IOStream*)(new ProtocolUncertainStream(mrStream)))
		:((IOStream*)(new PartialReadStream(mrStream, streamSize))));
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    Protocol::SendStream(IOStream &)
//		Purpose: Send a stream to the remote side
//		Created: 2003/08/26
//
// --------------------------------------------------------------------------
void Protocol::SendStream(IOStream &rStream)
{
	// Check usage
	if(mValidDataSize != -1 || mWriteOffset != -1 || mReadOffset != -1)
	{
		THROW_EXCEPTION(ServerException, Protocol_BadUsage)
	}

	// Handshake done?
	if(!mHandshakeDone)
	{
		Handshake();
	}

	// How should this be streamed?
	bool uncertainSize = false;
	IOStream::pos_type streamSize = rStream.BytesLeftToRead();
	if(streamSize == IOStream::SizeOfStreamUnknown
		|| streamSize > 0x7fffffff)
	{
		// Can't send this using the fixed size header
		uncertainSize = true;
	}
	
	// Inform sub class
	InformStreamSending(streamSize);
	
	// Make header
	PW_ObjectHeader objHeader;
	objHeader.mObjSize = htonl(uncertainSize?(ProtocolStream_SizeUncertain):streamSize);
	objHeader.mObjType = htonl(SPECIAL_STREAM_OBJECT_TYPE);

	// Write header
	mrStream.Write(&objHeader, sizeof(objHeader));
	// Could be sent in one of two ways
	if(uncertainSize)
	{
		// Don't know how big this is going to be -- so send it in chunks
		
		// Allocate memory
		uint8_t *blockA = (uint8_t *)malloc(UNCERTAIN_STREAM_SIZE_BLOCK + sizeof(int));
		if(blockA == 0)
		{
			throw std::bad_alloc();
		}
		uint8_t *block = blockA + sizeof(int);	// so that everything is word aligned for reading, but can put the one byte header before it
		
		try
		{
			int bytesInBlock = 0;
			while(rStream.StreamDataLeft())
			{
				// Read some of it
				bytesInBlock += rStream.Read(block + bytesInBlock, UNCERTAIN_STREAM_SIZE_BLOCK - bytesInBlock);
				
				// Send as much as we can out
				bytesInBlock -= SendStreamSendBlock(block, bytesInBlock);
			}

			// Everything recieved from stream, but need to send whatevers left in the block
			while(bytesInBlock > 0)
			{
				bytesInBlock -= SendStreamSendBlock(block, bytesInBlock);
			}
			
			// Send final byte to finish the stream
			uint8_t endOfStream = ProtocolStreamHeader_EndOfStream;
			mrStream.Write(&endOfStream, 1);
		}
		catch(...)
		{
			free(blockA);
			throw;
		}
		
		// Clean up
		free(blockA);
	}
	else
	{
		// Fixed size stream, send it all in one go
		if(!rStream.CopyStreamTo(mrStream, mTimeout, 4096 /* slightly larger buffer */))
		{
			THROW_EXCEPTION(ConnectionException, Conn_Protocol_TimeOutWhenSendingStream)
		}
	}
	// Make sure everything is written
	mrStream.WriteAllBuffered();
	
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    Protocol::SendStreamSendBlock(uint8_t *, int)
//		Purpose: Sends as much of the block as can be sent, moves the remainer down to the beginning,
//				 and returns the number of bytes sent. WARNING: Will write to Block[-1]
//		Created: 5/12/03
//
// --------------------------------------------------------------------------
int Protocol::SendStreamSendBlock(uint8_t *Block, int BytesInBlock)
{
	// Quick sanity check
	if(BytesInBlock == 0)
	{
		return 0;
	}
	
	// Work out the header byte
	uint8_t header = 0;
	int writeSize = 0;
	if(BytesInBlock >= (64*1024))
	{
		header = ProtocolStreamHeader_SizeIs64k;
		writeSize = (64*1024);
	}
	else
	{
		// Scan the table to find the most that can be written
		for(int s = ProtocolStreamHeader_MaxEncodedSizeValue; s > 0; --s)
		{
			if(sProtocolStreamHeaderLengths[s] <= BytesInBlock)
			{
				header = s;
				writeSize = sProtocolStreamHeaderLengths[s];
				break;
			}
		}
	}
	ASSERT(header > 0);
	
	// Store the header
	Block[-1] = header;
	
	// Write everything out
	mrStream.Write(Block - 1, writeSize + 1);
	
	// move the remainer to the beginning of the block for the next time round
	if(writeSize != BytesInBlock)
	{
		::memmove(Block, Block + writeSize, BytesInBlock - writeSize);
	}
	
	return writeSize;
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    Protocol::InformStreamReceiving(u_int32_t)
//		Purpose: Informs sub classes about streams being received
//		Created: 2003/10/27
//
// --------------------------------------------------------------------------
void Protocol::InformStreamReceiving(u_int32_t Size)
{
	// Do nothing
}

// --------------------------------------------------------------------------
//
// Function
//		Name:    Protocol::InformStreamSending(u_int32_t)
//		Purpose: Informs sub classes about streams being sent
//		Created: 2003/10/27
//
// --------------------------------------------------------------------------
void Protocol::InformStreamSending(u_int32_t Size)
{
	// Do nothing
}


/* 
perl code to generate the table below

#!/usr/bin/perl
use strict;
open OUT,">protolengths.txt";
my $len = 0;
for(0 .. 255)
{
	print OUT "\t$len,\t// $_\n";
	my $inc = 1;
	$inc = 8 if $_ >= 64;
	$inc = 16 if $_ >= 96;
	$inc = 32 if $_ >= 112;
	$inc = 64 if $_ >= 128;
	$inc = 128 if $_ >= 135;
	$inc = 256 if $_ >= 147;
	$inc = 512 if $_ >= 159;
	$inc = 1024 if $_ >= 231;
	$len += $inc;
}
close OUT;

*/
const uint16_t Protocol::sProtocolStreamHeaderLengths[256] =
{
	0,	// 0
	1,	// 1
	2,	// 2
	3,	// 3
	4,	// 4
	5,	// 5
	6,	// 6
	7,	// 7
	8,	// 8
	9,	// 9
	10,	// 10
	11,	// 11
	12,	// 12
	13,	// 13
	14,	// 14
	15,	// 15
	16,	// 16
	17,	// 17
	18,	// 18
	19,	// 19
	20,	// 20
	21,	// 21
	22,	// 22
	23,	// 23
	24,	// 24
	25,	// 25
	26,	// 26
	27,	// 27
	28,	// 28
	29,	// 29
	30,	// 30
	31,	// 31
	32,	// 32
	33,	// 33
	34,	// 34
	35,	// 35
	36,	// 36
	37,	// 37
	38,	// 38
	39,	// 39
	40,	// 40
	41,	// 41
	42,	// 42
	43,	// 43
	44,	// 44
	45,	// 45
	46,	// 46
	47,	// 47
	48,	// 48
	49,	// 49
	50,	// 50
	51,	// 51
	52,	// 52
	53,	// 53
	54,	// 54
	55,	// 55
	56,	// 56
	57,	// 57
	58,	// 58
	59,	// 59
	60,	// 60
	61,	// 61
	62,	// 62
	63,	// 63
	64,	// 64
	72,	// 65
	80,	// 66
	88,	// 67
	96,	// 68
	104,	// 69
	112,	// 70
	120,	// 71
	128,	// 72
	136,	// 73
	144,	// 74
	152,	// 75
	160,	// 76
	168,	// 77
	176,	// 78
	184,	// 79
	192,	// 80
	200,	// 81
	208,	// 82
	216,	// 83
	224,	// 84
	232,	// 85
	240,	// 86
	248,	// 87
	256,	// 88
	264,	// 89
	272,	// 90
	280,	// 91
	288,	// 92
	296,	// 93
	304,	// 94
	312,	// 95
	320,	// 96
	336,	// 97
	352,	// 98
	368,	// 99
	384,	// 100
	400,	// 101
	416,	// 102
	432,	// 103
	448,	// 104
	464,	// 105
	480,	// 106
	496,	// 107
	512,	// 108
	528,	// 109
	544,	// 110
	560,	// 111
	576,	// 112
	608,	// 113
	640,	// 114
	672,	// 115
	704,	// 116
	736,	// 117
	768,	// 118
	800,	// 119
	832,	// 120
	864,	// 121
	896,	// 122
	928,	// 123
	960,	// 124
	992,	// 125
	1024,	// 126
	1056,	// 127
	1088,	// 128
	1152,	// 129
	1216,	// 130
	1280,	// 131
	1344,	// 132
	1408,	// 133
	1472,	// 134
	1536,	// 135
	1664,	// 136
	1792,	// 137
	1920,	// 138
	2048,	// 139
	2176,	// 140
	2304,	// 141
	2432,	// 142
	2560,	// 143
	2688,	// 144
	2816,	// 145
	2944,	// 146
	3072,	// 147
	3328,	// 148
	3584,	// 149
	3840,	// 150
	4096,	// 151
	4352,	// 152
	4608,	// 153
	4864,	// 154
	5120,	// 155
	5376,	// 156
	5632,	// 157
	5888,	// 158
	6144,	// 159
	6656,	// 160
	7168,	// 161
	7680,	// 162
	8192,	// 163
	8704,	// 164
	9216,	// 165
	9728,	// 166
	10240,	// 167
	10752,	// 168
	11264,	// 169
	11776,	// 170
	12288,	// 171
	12800,	// 172
	13312,	// 173
	13824,	// 174
	14336,	// 175
	14848,	// 176
	15360,	// 177
	15872,	// 178
	16384,	// 179
	16896,	// 180
	17408,	// 181
	17920,	// 182
	18432,	// 183
	18944,	// 184
	19456,	// 185
	19968,	// 186
	20480,	// 187
	20992,	// 188
	21504,	// 189
	22016,	// 190
	22528,	// 191
	23040,	// 192
	23552,	// 193
	24064,	// 194
	24576,	// 195
	25088,	// 196
	25600,	// 197
	26112,	// 198
	26624,	// 199
	27136,	// 200
	27648,	// 201
	28160,	// 202
	28672,	// 203
	29184,	// 204
	29696,	// 205
	30208,	// 206
	30720,	// 207
	31232,	// 208
	31744,	// 209
	32256,	// 210
	32768,	// 211
	33280,	// 212
	33792,	// 213
	34304,	// 214
	34816,	// 215
	35328,	// 216
	35840,	// 217
	36352,	// 218
	36864,	// 219
	37376,	// 220
	37888,	// 221
	38400,	// 222
	38912,	// 223
	39424,	// 224
	39936,	// 225
	40448,	// 226
	40960,	// 227
	41472,	// 228
	41984,	// 229
	42496,	// 230
	43008,	// 231
	44032,	// 232
	45056,	// 233
	46080,	// 234
	47104,	// 235
	48128,	// 236
	49152,	// 237
	50176,	// 238
	51200,	// 239
	52224,	// 240
	53248,	// 241
	54272,	// 242
	55296,	// 243
	56320,	// 244
	57344,	// 245
	58368,	// 246
	59392,	// 247
	60416,	// 248
	61440,	// 249
	62464,	// 250
	63488,	// 251
	64512,	// 252
	0,		// 253 = 65536 / 64k
	0,		// 254 = special (reserved)
	0		// 255 = special (reserved)
};




