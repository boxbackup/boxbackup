#include "autogen_TestProtocol.h"

class TestContext
{
public:
	TestContext();
	~TestContext();
	std::auto_ptr<TestProtocolMessage> DoCommandHook(TestProtocolMessage& command,
		TestProtocolReplyable& protocol, IOStream* data_stream = NULL)
	{
		return std::auto_ptr<TestProtocolMessage>();
	}
};
