// distribution boxbackup-0.09
// 
//  
// Copyright (c) 2003, 2004
//      Ben Summers.  All rights reserved.
//  
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
// 1. Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
// 3. All use of this software and associated advertising materials must 
//    display the following acknowledgement:
//        This product includes software developed by Ben Summers.
// 4. The names of the Authors may not be used to endorse or promote
//    products derived from this software without specific prior written
//    permission.
// 
// [Where legally impermissible the Authors do not disclaim liability for 
// direct physical injury or death caused solely by defects in the software 
// unless it is modified by a third party.]
// 
// THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
// IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT,
// INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
// HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
// ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//  
//  
//  

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

