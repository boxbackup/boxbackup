// --------------------------------------------------------------------------
//
// File
//		Name:    Protocol.h
//		Purpose: Generic protocol support
//		Created: 2003/08/19
//
// --------------------------------------------------------------------------

#ifndef PROTOCOL__H
#define PROTOCOL__H

#include <sys/types.h>

#include <memory>
#include <vector>
#include <string>

#include "Message.h"

class IOStream;
class SocketStream;

// default timeout is 15 minutes
#define PROTOCOL_DEFAULT_TIMEOUT	(15*60*1000)
// 16 default maximum object size -- should be enough
#define PROTOCOL_DEFAULT_MAXOBJSIZE	(16*1024)

// --------------------------------------------------------------------------
//
// Class
//		Name:    Protocol
//		Purpose: Generic command / response protocol support
//		Created: 2003/08/19
//
// --------------------------------------------------------------------------
class Protocol
{
public:
	Protocol(std::auto_ptr<SocketStream> apConn);
	virtual ~Protocol();
	
private:
	Protocol(const Protocol &rToCopy);

protected:
	// Unsafe to make public, as they may allow sending objects
	// from a different protocol. The derived class prevents this.
	std::auto_ptr<Message> ReceiveInternal();
	void SendInternal(const Message &rObject);

public:
	void Handshake();
	std::auto_ptr<IOStream> ReceiveStream();
	void SendStream(IOStream &rStream);
	
	enum
	{
		NoError = -1,
		UnknownError = 0
	};

	// --------------------------------------------------------------------------
	//
	// Function
	//		Name:    Protocol::SetTimeout(int)
	//		Purpose: Sets the timeout for sending and reciving
	//		Created: 2003/08/19
	//
	// --------------------------------------------------------------------------
	void SetTimeout(int NewTimeout) {mTimeout = NewTimeout;}
	
	
	// --------------------------------------------------------------------------
	//
	// Function
	//		Name:    Protocol::GetTimeout()
	//		Purpose: Get current timeout for sending and receiving
	//		Created: 2003/09/06
	//
	// --------------------------------------------------------------------------
	int GetTimeout() {return mTimeout;}

	// --------------------------------------------------------------------------
	//
	// Function
	//		Name:    Protocol::SetMaxObjectSize(int)
	//		Purpose: Sets the maximum size of an object which will be accepted
	//		Created: 2003/08/19
	//
	// --------------------------------------------------------------------------	
	void SetMaxObjectSize(unsigned int NewMaxObjSize) {mMaxObjectSize = NewMaxObjSize;}

	// For Message derived classes
	void Read(void *Buffer, int Size);
	void Read(std::string &rOut, int Size);
	void Read(int64_t &rOut);
	void Read(int32_t &rOut);
	void Read(int16_t &rOut);
	void Read(int8_t &rOut);
	void Read(bool &rOut) {int8_t read; Read(read); rOut = (read == true);}
	void Read(std::string &rOut);
	template<typename type>
	void Read(type &rOut)
	{
		rOut.ReadFromProtocol(*this);
	}
	// --------------------------------------------------------------------------
	//
	// Function
	//		Name:    Protocol::ReadVector(std::vector<> &)
	//		Purpose: Reads a vector/list of items from the stream
	//		Created: 2003/08/19
	//
	// --------------------------------------------------------------------------
	template<typename type>
	void ReadVector(std::vector<type> &rOut)
	{
		rOut.clear();
		int16_t num = 0;
		Read(num);
		for(int16_t n = 0; n < num; ++n)
		{
			type v;
			Read(v);
			rOut.push_back(v);
		}
	}
	
	void Write(const void *Buffer, int Size);
	void Write(int64_t Value);
	void Write(int32_t Value);
	void Write(int16_t Value);
	void Write(int8_t Value);
	void Write(bool Value) {int8_t write = Value; Write(write);}
	void Write(const std::string &rValue);
	template<typename type>
	void Write(const type &rValue)
	{
		rValue.WriteToProtocol(*this);
	}
	template<typename type>
	// --------------------------------------------------------------------------
	//
	// Function
	//		Name:    Protocol::WriteVector(const std::vector<> &)
	//		Purpose: Writes a vector/list of items from the stream
	//		Created: 2003/08/19
	//
	// --------------------------------------------------------------------------
	void WriteVector(const std::vector<type> &rValue)
	{
		int16_t num = rValue.size();
		Write(num);
		for(int16_t n = 0; n < num; ++n)
		{
			Write(rValue[n]);
		}
	}

public:
	static const uint16_t sProtocolStreamHeaderLengths[256];
	enum
	{
		ProtocolStreamHeader_EndOfStream = 0,
		ProtocolStreamHeader_MaxEncodedSizeValue = 252,
		ProtocolStreamHeader_SizeIs64k = 253,
		ProtocolStreamHeader_Reserved1 = 254,
		ProtocolStreamHeader_Reserved2 = 255
	};
	enum
	{
		ProtocolStream_SizeUncertain = 0xffffffff
	};
	bool GetLogToSysLog() { return mLogToSysLog; }
	FILE *GetLogToFile() { return mLogToFile; }
	void SetLogToSysLog(bool Log = false) {mLogToSysLog = Log;}
	void SetLogToFile(FILE *File = 0) {mLogToFile = File;}
	int64_t GetBytesRead() const;
	int64_t GetBytesWritten() const;

protected:
	virtual std::auto_ptr<Message> MakeMessage(int ObjType) = 0;
	virtual const char *GetProtocolIdentString() = 0;
	
	void CheckAndReadHdr(void *hdr);	// don't use type here to avoid dependency
	
	// Will be used for logging
	virtual void InformStreamReceiving(uint32_t Size);
	virtual void InformStreamSending(uint32_t Size);
	
private:
	void EnsureBufferAllocated(int Size);
	int SendStreamSendBlock(uint8_t *Block, int BytesInBlock);

	std::auto_ptr<SocketStream> mapConn;
	bool mHandshakeDone;
	unsigned int mMaxObjectSize;
	int mTimeout;
	char *mpBuffer;
	int mBufferSize;
	int mReadOffset;
	int mWriteOffset;
	int mValidDataSize;
	bool mLogToSysLog;
	FILE *mLogToFile;
};

class ProtocolContext
{
};

#endif // PROTOCOL__H
