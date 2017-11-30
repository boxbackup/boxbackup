
#include "Box.h"

#ifdef HAVE_SYSLOG_H
#include <syslog.h>
#endif

#include "autogen_TestProtocol.h"
#include "CollectInBufferStream.h"

#include "MemLeakFindOn.h"


std::auto_ptr<TestProtocolMessage> TestProtocolReplyable::HandleException(BoxException& e) const
{
	throw;
}

std::auto_ptr<TestProtocolMessage> TestProtocolHello::DoCommand(TestProtocolReplyable &rProtocol, TestContext &rContext) const
{
	if(mNumber32 != 41 || mNumber16 != 87 || mNumber8 != 11 || mText != "pingu")
	{
		return std::auto_ptr<TestProtocolMessage>(new TestProtocolError(0, 0));
	}
	return std::auto_ptr<TestProtocolMessage>(new TestProtocolHello(12,89,22,std::string("Hello world!")));
}

std::auto_ptr<TestProtocolMessage> TestProtocolLists::DoCommand(TestProtocolReplyable &rProtocol, TestContext &rContext) const
{
	return std::auto_ptr<TestProtocolMessage>(new TestProtocolListsReply(mLotsOfText.size()));
}

std::auto_ptr<TestProtocolMessage> TestProtocolQuit::DoCommand(TestProtocolReplyable &rProtocol, TestContext &rContext) const
{
	return std::auto_ptr<TestProtocolMessage>(new TestProtocolQuit);
}

std::auto_ptr<TestProtocolMessage> TestProtocolSimple::DoCommand(TestProtocolReplyable &rProtocol, TestContext &rContext) const
{
	return std::auto_ptr<TestProtocolMessage>(new TestProtocolSimpleReply(mValue+1));
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

std::auto_ptr<TestProtocolMessage> TestProtocolGetStream::DoCommand(TestProtocolReplyable &rProtocol, TestContext &rContext) const
{
	// make a new stream object
	std::auto_ptr<CollectInBufferStream> apStream(
		mUncertainSize?(new UncertainBufferStream):(new CollectInBufferStream));
	
	// Data.
	int values[24273];
	int v = mStartingValue;
	for(int l = 0; l < 3; ++l)
	{
		for(int x = 0; x < 24273; ++x)
		{
			values[x] = v++;
		}
		apStream->Write(values, sizeof(values));
	}
	
	// Finished
	apStream->SetForReading();
	
	// Get it to be sent
	rProtocol.SendStreamAfterCommand((std::auto_ptr<IOStream>)apStream);

	return std::auto_ptr<TestProtocolMessage>(new TestProtocolGetStream(mStartingValue, mUncertainSize));
}

std::auto_ptr<TestProtocolMessage> TestProtocolSendStream::DoCommand(
	TestProtocolReplyable &rProtocol, TestContext &rContext,
	IOStream& rDataStream) const
{
	if(mValue != 0x73654353298ffLL)
	{
		return std::auto_ptr<TestProtocolMessage>(new TestProtocolError(0, 0));
	}
	
	// Get a stream
	bool uncertain = (rDataStream.BytesLeftToRead() == IOStream::SizeOfStreamUnknown);
	
	// Count how many bytes in it
	int bytes = 0;
	char buffer[125];
	while(rDataStream.StreamDataLeft())
	{
		bytes += rDataStream.Read(buffer, sizeof(buffer));
	}

	// tell the caller how many bytes there were
	return std::auto_ptr<TestProtocolMessage>(new TestProtocolGetStream(bytes, uncertain));
}

std::auto_ptr<TestProtocolMessage> TestProtocolString::DoCommand(TestProtocolReplyable &rProtocol, TestContext &rContext) const
{
	return std::auto_ptr<TestProtocolMessage>(new TestProtocolString(mTest));
}

std::auto_ptr<TestProtocolMessage> TestProtocolDeliberateError::DoCommand(
	TestProtocolReplyable &rProtocol, TestContext &rContext) const
{
	return std::auto_ptr<TestProtocolMessage>(new TestProtocolError(
		TestProtocolError::ErrorType,
		TestProtocolError::Err_DeliberateError));
}

std::auto_ptr<TestProtocolMessage> TestProtocolUnexpectedError::DoCommand(
	TestProtocolReplyable &rProtocol, TestContext &rContext) const
{
	THROW_EXCEPTION_MESSAGE(CommonException, Internal, "unexpected error");
}
