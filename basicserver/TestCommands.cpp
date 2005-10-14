
#include "Box.h"

#include <syslog.h>

#include "autogen_TestProtocolServer.h"
#include "CollectInBufferStream.h"

#include "MemLeakFindOn.h"


std::auto_ptr<ProtocolObject> TestProtocolServerHello::DoCommand(TestProtocolServer &rProtocol, TestContext &rContext)
{
	if(mNumber32 != 41 || mNumber16 != 87 || mNumber8 != 11 || mText != "pingu")
	{
		return std::auto_ptr<ProtocolObject>(new TestProtocolServerError(0, 0));
	}
	return std::auto_ptr<ProtocolObject>(new TestProtocolServerHello(12,89,22,std::string("Hello world!")));
}

std::auto_ptr<ProtocolObject> TestProtocolServerLists::DoCommand(TestProtocolServer &rProtocol, TestContext &rContext)
{
	return std::auto_ptr<ProtocolObject>(new TestProtocolServerListsReply(mLotsOfText.size()));
}

std::auto_ptr<ProtocolObject> TestProtocolServerQuit::DoCommand(TestProtocolServer &rProtocol, TestContext &rContext)
{
	return std::auto_ptr<ProtocolObject>(new TestProtocolServerQuit);
}

std::auto_ptr<ProtocolObject> TestProtocolServerSimple::DoCommand(TestProtocolServer &rProtocol, TestContext &rContext)
{
	return std::auto_ptr<ProtocolObject>(new TestProtocolServerSimpleReply(mValue+1));
}

class UncertainBufferStream : public CollectInBufferStream
{
public:
	// make the collect in buffer stream pretend not to know how many bytes are left
	pos_type BytesLeftToRead()
	{
		return IOStream::SizeOfStreamUnknown;
	}
};

std::auto_ptr<ProtocolObject> TestProtocolServerGetStream::DoCommand(TestProtocolServer &rProtocol, TestContext &rContext)
{
	// make a new stream object
	CollectInBufferStream *pstream = mUncertainSize?(new UncertainBufferStream):(new CollectInBufferStream);
	
	// Data.
	int values[24273];
	int v = mStartingValue;
	for(int l = 0; l < 3; ++l)
	{
		for(int x = 0; x < 24273; ++x)
		{
			values[x] = v++;
		}
		pstream->Write(values, sizeof(values));
	}
	
	// Finished
	pstream->SetForReading();
	
	// Get it to be sent
	rProtocol.SendStreamAfterCommand(pstream);

	return std::auto_ptr<ProtocolObject>(new TestProtocolServerGetStream(mStartingValue, mUncertainSize));
}

std::auto_ptr<ProtocolObject> TestProtocolServerSendStream::DoCommand(TestProtocolServer &rProtocol, TestContext &rContext)
{
	if(mValue != 0x73654353298ffLL)
	{
		return std::auto_ptr<ProtocolObject>(new TestProtocolServerError(0, 0));
	}
	
	// Get a stream
	std::auto_ptr<IOStream> stream(rProtocol.ReceiveStream());
	bool uncertain = (stream->BytesLeftToRead() == IOStream::SizeOfStreamUnknown);
	
	// Count how many bytes in it
	int bytes = 0;
	char buffer[125];
	while(stream->StreamDataLeft())
	{
		bytes += stream->Read(buffer, sizeof(buffer));
	}

	// tell the caller how many bytes there were
	return std::auto_ptr<ProtocolObject>(new TestProtocolServerGetStream(bytes, uncertain));
}

std::auto_ptr<ProtocolObject> TestProtocolServerString::DoCommand(TestProtocolServer &rProtocol, TestContext &rContext)
{
	return std::auto_ptr<ProtocolObject>(new TestProtocolServerString(mTest));
}

