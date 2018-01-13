// --------------------------------------------------------------------------
//
// File
//		Name:    testbasicserver.cpp
//		Purpose: Test basic server classes
//		Created: 2003/07/29
//
// --------------------------------------------------------------------------


#include "Box.h"

#ifdef HAVE_SIGNAL_H
#	include <signal.h>
#endif

#include <stdio.h>
#include <time.h>

#include <typeinfo>

#include "Test.h"
#include "Daemon.h"
#include "Configuration.h"
#include "ServerStream.h"
#include "SocketStream.h"
#include "IOStreamGetLine.h"
#include "ServerTLS.h"
#include "CollectInBufferStream.h"

#include "TestContext.h"
#include "autogen_TestProtocol.h"
#include "ServerControl.h"

#include "MemLeakFindOn.h"

#define SERVER_LISTEN_PORT	2003

// in ms
#define COMMS_READ_TIMEOUT	4
#define COMMS_SERVER_WAIT_BEFORE_REPLYING	1000
// Use a longer timeout to give Srv2TestConversations time to write 20 MB to each of
// three child processes before starting to read it back again, without the children
// timing out and aborting.
#define SHORT_TIMEOUT 30000

class basicdaemon : public Daemon
{
public:
basicdaemon() {};
~basicdaemon() {}
virtual void Run();
};

void basicdaemon::Run()
{
	// Write a file to check it's done...
	const Configuration &c(GetConfiguration());
	
	FILE *f = fopen(c.GetKeyValue("TestFile").c_str(), "w");
	fclose(f);

	while(!StopRun())
	{
		::sleep(10);
	}
}

void testservers_pause_before_reply()
{
#ifdef WIN32
	Sleep(COMMS_SERVER_WAIT_BEFORE_REPLYING);
#else
	int64_t nsec = COMMS_SERVER_WAIT_BEFORE_REPLYING * 1000LL * 1000;	// convert to ns
	struct timespec t;
	t.tv_sec = nsec / NANO_SEC_IN_SEC;
	t.tv_nsec = nsec % NANO_SEC_IN_SEC;
	::nanosleep(&t, NULL);
#endif
}

#define LARGE_DATA_BLOCK_SIZE 19870
#define LARGE_DATA_SIZE (LARGE_DATA_BLOCK_SIZE*1000)

void testservers_connection(SocketStream &rStream)
{
	IOStreamGetLine getline(rStream);

	if(typeid(rStream) == typeid(SocketStreamTLS))
	{
		// need to wait for some data before sending stuff, otherwise timeout test doesn't work
		std::string line = getline.GetLine(false); // !preprocess
		SocketStreamTLS &rtls = (SocketStreamTLS&)rStream;
		std::string line1("CONNECTED:");
		line1 += rtls.GetPeerCommonName();
		line1 += '\n';

		// Reply after a short delay, to allow the client to test timing out
		// in GetLine():
		testservers_pause_before_reply();

		rStream.Write(line1.c_str(), line1.size());
	}

	while(!getline.IsEOF())
	{
		std::string line = getline.GetLine(false); // !preprocess
		if(line == "QUIT")
		{
			break;
		}
		if(line == "LARGEDATA")
		{
			// This part of the test is timing-sensitive, because we write
			// 20 MB to the test and then have to wait while it reads 20 MB
			// from the other two children before writing anything back to us.
			// We could timeout waiting for it to talk to us again. So we
			// increased the SHORT_TIMEOUT from 5 seconds to 30 to allow 
			// more time.
			{
				// Send lots of data
				char data[LARGE_DATA_BLOCK_SIZE];
				for(unsigned int y = 0; y < sizeof(data); y++)
				{
					data[y] = y & 0xff;
				}
				for(int s = 0; s < (LARGE_DATA_SIZE / LARGE_DATA_BLOCK_SIZE); ++s)
				{
					rStream.Write(data, sizeof(data), SHORT_TIMEOUT);
				}
			}
			{
				// Receive lots of data
				char buf[1024];
				int total = 0;
				int r = 0;
				while(total < LARGE_DATA_SIZE &&
					(r = rStream.Read(buf, sizeof(buf), SHORT_TIMEOUT)) != 0)
				{
					total += r;
				}
				TEST_THAT(total == LARGE_DATA_SIZE);
				if (total != LARGE_DATA_SIZE)
				{
					BOX_ERROR("Expected " << 
						LARGE_DATA_SIZE << " bytes " <<
						"but was " << total);
					return;
				}
			}
			{
				// Send lots of data again
				char data[LARGE_DATA_BLOCK_SIZE];
				for(unsigned int y = 0; y < sizeof(data); y++)
				{
					data[y] = y & 0xff;
				}
				for(int s = 0; s < (LARGE_DATA_SIZE / LARGE_DATA_BLOCK_SIZE); ++s)
				{
					rStream.Write(data, sizeof(data), SHORT_TIMEOUT);
				}
			}
			
			// next!
			continue;
		}
		std::string backwards;
		for(std::string::const_reverse_iterator i(line.end()); i != std::string::const_reverse_iterator(line.begin()); ++i)
		{
			backwards += (*i);
		}
		backwards += '\n';
		testservers_pause_before_reply();
		rStream.Write(backwards.c_str(), backwards.size());
	}
	rStream.Shutdown();
	rStream.Close();
}



class testserver : public ServerStream<SocketStream, SERVER_LISTEN_PORT>
{
public:
	testserver() {}
	~testserver() {}
	
	void Connection(std::auto_ptr<SocketStream> apStream);
	
	virtual const char *DaemonName() const
	{
		return "test-srv2";
	}
	const ConfigurationVerify *GetConfigVerify() const;

};

const ConfigurationVerify *testserver::GetConfigVerify() const
{
	static ConfigurationVerifyKey verifyserverkeys[] = 
	{
		SERVERSTREAM_VERIFY_SERVER_KEYS(ConfigurationVerifyKey::NoDefaultValue) // no default listen addresses
	};

	static ConfigurationVerify verifyserver[] = 
	{
		{
			"Server", /* mName */
			0, /* mpSubConfigurations */
			verifyserverkeys, /* mpKeys */
			ConfigTest_Exists | ConfigTest_LastEntry,
			0
		}
	};

	static ConfigurationVerify verify =
	{
		"root", /* mName */
		verifyserver, /* mpSubConfigurations */
		0, /* mpKeys */
		ConfigTest_Exists | ConfigTest_LastEntry,
		0
	};

	return &verify;
}

void testserver::Connection(std::auto_ptr<SocketStream> apStream)
{
	testservers_connection(*apStream);
}

class testProtocolServer : public testserver
{
public:
	testProtocolServer() {}
	~testProtocolServer() {}

	void Connection(std::auto_ptr<SocketStream> apStream);
	
	virtual const char *DaemonName() const
	{
		return "test-srv4";
	}
};

void testProtocolServer::Connection(std::auto_ptr<SocketStream> apStream)
{
	TestProtocolServer server(apStream);
	TestContext context;
	server.DoServer(context);
}


class testTLSserver : public ServerTLS<SERVER_LISTEN_PORT>
{
public:
	testTLSserver() {}
	~testTLSserver() {}
	
	void Connection(std::auto_ptr<SocketStreamTLS> apStream);
	
	virtual const char *DaemonName() const
	{
		return "test-srv3";
	}
	const ConfigurationVerify *GetConfigVerify() const;

};

const ConfigurationVerify *testTLSserver::GetConfigVerify() const
{
	static ConfigurationVerifyKey verifyserverkeys[] = 
	{
		SERVERTLS_VERIFY_SERVER_KEYS(ConfigurationVerifyKey::NoDefaultValue) // no default listen addresses
	};

	static ConfigurationVerify verifyserver[] = 
	{
		{
			"Server",
			0,
			verifyserverkeys,
			ConfigTest_Exists | ConfigTest_LastEntry,
			0
		}
	};

	static ConfigurationVerify verify =
	{
		"root",
		verifyserver,
		0,
		ConfigTest_Exists | ConfigTest_LastEntry,
		0
	};

	return &verify;
}

void testTLSserver::Connection(std::auto_ptr<SocketStreamTLS> apStream)
{
	testservers_connection(*apStream);
}


void Srv2TestConversations(const std::vector<IOStream *> &conns)
{
	const static char *tosend[] = {
		"test 1\n", "carrots\n", "pineapples\n", "booo!\n", 0
	};
	const static char *recieve[] = {
		"1 tset", "storrac", "selppaenip", "!ooob", 0
	};
	
	IOStreamGetLine **getline = new IOStreamGetLine*[conns.size()];
	for(unsigned int c = 0; c < conns.size(); ++c)
	{
		getline[c] = new IOStreamGetLine(*conns[c]);
		
		if(typeid(*conns[c]) == typeid(SocketStreamTLS))
		{
			SocketStreamTLS *ptls = (SocketStreamTLS *)conns[c];
			BOX_INFO("Connected to '" << ptls->GetPeerCommonName() << "'");
		
			// Send some data, any data, to get the first response.
			conns[c]->Write("Hello\n", 6);
		
			// First read should timeout, while server sleeps in
			// testservers_pause_before_reply():
			TEST_CHECK_THROWS(getline[c]->GetLine(false,
				COMMS_SERVER_WAIT_BEFORE_REPLYING * 0.5),
				CommonException, IOStreamTimedOut);

			// Second read should not timeout, because we should have waited
			// COMMS_SERVER_WAIT_BEFORE_REPLYING * 1.5
			std::string line1 = getline[c]->GetLine(false,
				COMMS_SERVER_WAIT_BEFORE_REPLYING);
			TEST_EQUAL(line1, "CONNECTED:CLIENT");
		}
	}
	
	for(int q = 0; tosend[q] != 0; ++q)
	{
		for(unsigned int c = 0; c < conns.size(); ++c)
		{
			//printf("%d: %s", c, tosend[q]);
			conns[c]->Write(tosend[q], strlen(tosend[q]));
			std::string rep;
			bool hadTimeout = false;
			while(true)
			{
				try
				{
					// COMMS_READ_TIMEOUT is very short, so we will get a lot of these:
					HideSpecificExceptionGuard guard(CommonException::ExceptionType,
						CommonException::IOStreamTimedOut);
					rep = getline[c]->GetLine(false, COMMS_READ_TIMEOUT);
					break;
				}
				catch(BoxException &e)
				{
					if(EXCEPTION_IS_TYPE(e, CommonException, IOStreamTimedOut))
					{
						hadTimeout = true;
					}
					else if(EXCEPTION_IS_TYPE(e, CommonException, SignalReceived))
					{
						// just try again
					}
					else
					{
						throw;
					}
				}
			}
			TEST_EQUAL_LINE(rep, recieve[q], "Line " << q);
			TEST_LINE(hadTimeout, "Line " << q)
		}
	}
	for(unsigned int c = 0; c < conns.size(); ++c)
	{
		conns[c]->Write("LARGEDATA\n", 10, SHORT_TIMEOUT);
	}
	// This part of the test is timing-sensitive, because we read 20 MB from each of
	// three daemon processes, then write 20 MB to each of them, then read back 
	// another 20 MB from each of them. Each child could timeout waiting for us to
	// read from it, or write to it, while we're servicing another child. So we
	// increased the SHORT_TIMEOUT from 5 seconds to 30 to allow enough time.
	for(unsigned int c = 0; c < conns.size(); ++c)
	{
		// Receive lots of data
		char buf[1024];
		int total = 0;
		int r = 0;
		while(total < LARGE_DATA_SIZE &&
			(r = conns[c]->Read(buf, sizeof(buf), SHORT_TIMEOUT)) != 0)
		{
			total += r;
		}
		TEST_THAT(total == LARGE_DATA_SIZE);
	}
	for(unsigned int c = 0; c < conns.size(); ++c)
	{
		// Send lots of data
		char data[LARGE_DATA_BLOCK_SIZE];
		for(unsigned int y = 0; y < sizeof(data); y++)
		{
			data[y] = y & 0xff;
		}
		for(int s = 0; s < (LARGE_DATA_SIZE / LARGE_DATA_BLOCK_SIZE); ++s)
		{
			conns[c]->Write(data, sizeof(data), SHORT_TIMEOUT);
		}
	}
	for(unsigned int c = 0; c < conns.size(); ++c)
	{
		// Receive lots of data again
		char buf[1024];
		int total = 0;
		int r = 0;
		while(total < LARGE_DATA_SIZE &&
			(r = conns[c]->Read(buf, sizeof(buf), SHORT_TIMEOUT)) != 0)
		{
			total += r;
		}
		TEST_THAT(total == LARGE_DATA_SIZE);
	}

	for(unsigned int c = 0; c < conns.size(); ++c)
	{
		conns[c]->Write("QUIT\n", 5);
	}
	
	for(unsigned int c = 0; c < conns.size(); ++c)
	{
		if ( getline[c] ) delete getline[c];
		getline[c] = 0;
	}
	if ( getline ) delete [] getline;
	getline = 0;
}

void TestStreamReceive(TestProtocolClient &protocol, int value, bool uncertainstream)
{
	std::auto_ptr<TestProtocolGetStream> reply(protocol.QueryGetStream(value, uncertainstream));
	TEST_THAT(reply->GetStartingValue() == value);
	
	// Get a stream
	std::auto_ptr<IOStream> stream(protocol.ReceiveStream());
	
	// check uncertainty
	TEST_THAT(uncertainstream == (stream->BytesLeftToRead() == IOStream::SizeOfStreamUnknown));
	
	printf("stream is %s\n", uncertainstream?"uncertain size":"fixed size");
	
	// Then check the contents
	int values[998];
	int v = value;
	int count = 0;
	int bytesleft = 0;
	int bytessofar = 0;
	while(stream->StreamDataLeft())
	{
		// Read some data
		int bytes = stream->Read(((char*)values) + bytesleft,
			sizeof(values) - bytesleft, SHORT_TIMEOUT);
		bytessofar += bytes;
		bytes += bytesleft;
		int n = bytes / 4;
		//printf("read %d, n = %d, so far = %d\n", bytes, n, bytessofar);
		for(int t = 0; t < n; ++t)
		{
			if(values[t] != v) printf("%d, %d, %d\n", t, values[t], v);
			TEST_THAT(values[t] == v++);
		}
		count += n;
		bytesleft = bytes - (n*4);
		if(bytesleft) ::memmove(values, ((char*)values) + bytes - bytesleft, bytesleft);
	}
	
	TEST_THAT(bytesleft == 0);
	TEST_THAT(count == (24273*3));	// over 64 k of data, definately
}


int test(int argc, const char *argv[])
{
	// Timing information is very useful for debugging race conditions during this test,
	// so enable it unconditionally:
	Console::SetShowTime(true);

	// Server launching stuff
	if(argc >= 2)
	{
		// this is a quick hack to allow passing some options
		// to the daemon

		const char* mode = argv[1];

		if (test_args.length() > 0)
		{
			argv[1] = test_args.c_str();
		}
		else
		{
			argc--;
			argv++;
		}

		if(strcmp(mode, "srv1") == 0)
		{
			// Run very basic daemon
			basicdaemon daemon;
			return daemon.Main("doesnotexist", argc, argv);
		}
		else if(strcmp(mode, "srv2") == 0)
		{
			// Run daemon which accepts connections
			testserver daemon;
			return daemon.Main("doesnotexist", argc, argv);
		}		
		else if(strcmp(mode, "srv3") == 0)
		{
			testTLSserver daemon;
			return daemon.Main("doesnotexist", argc, argv);
		}
		else if(strcmp(mode, "srv4") == 0)
		{
			testProtocolServer daemon;
			return daemon.Main("doesnotexist", argc, argv);
		}
	}

#ifndef WIN32
	// Don't die quietly if the server dies while we're communicating with it
	signal(SIGPIPE, SIG_IGN);
#endif

	//printf("SKIPPING TESTS------------------------\n");
	//goto protocolserver;

	// Launch a basic server
	{
		std::string cmd = TEST_EXECUTABLE " --test-daemon-args=";
		cmd += test_args;
		cmd += " srv1 testfiles/srv1.conf";
		int pid = LaunchServer(cmd, "testfiles/srv1.pid");

		TEST_THAT(pid != -1 && pid != 0);
		if(pid > 0)
		{
			// Check that it's written the expected file
			TEST_THAT(TestFileExists("testfiles" 
				DIRECTORY_SEPARATOR "srv1.test1"));
			TEST_THAT(ServerIsAlive(pid));

			// Move the config file over
			#ifdef WIN32
				TEST_THAT(EMU_UNLINK("testfiles"
					DIRECTORY_SEPARATOR "srv1.conf") != -1);
			#endif

			TEST_THAT(::rename(
				"testfiles" DIRECTORY_SEPARATOR "srv1b.conf", 
				"testfiles" DIRECTORY_SEPARATOR "srv1.conf") 
				!= -1);

			#ifndef WIN32
				// Get it to reread the config file
				TEST_THAT(HUPServer(pid));
				::sleep(1);
				TEST_THAT(ServerIsAlive(pid));
				// Check that new file exists
				TEST_THAT(TestFileExists("testfiles" 
					DIRECTORY_SEPARATOR "srv1.test2"));
			#endif // !WIN32

			// Kill it off
			TEST_THAT(KillServer(pid));

			#ifndef WIN32
				TestRemoteProcessMemLeaks(
					"generic-daemon.memleaks");
			#endif // !WIN32
		}
	}
	
	// Launch a test forking server
	{
		std::string cmd = TEST_EXECUTABLE " --test-daemon-args=";
		cmd += test_args;
		cmd += " srv2 testfiles/srv2.conf";
		int pid = LaunchServer(cmd, "testfiles/srv2.pid");

		TEST_THAT(pid != -1 && pid != 0);

		if(pid > 0)
		{
			// Will it restart?
			TEST_THAT(ServerIsAlive(pid));

			#ifndef WIN32
				TEST_THAT(HUPServer(pid));
				::sleep(1);
				TEST_THAT(ServerIsAlive(pid));
			#endif // !WIN32

			// Make some connections
			{
				SocketStream conn1;
				conn1.Open(Socket::TypeINET, "localhost", 2003);

				#ifndef WIN32
					SocketStream conn2;
					conn2.Open(Socket::TypeUNIX, 
						"testfiles/srv2.sock");
					SocketStream conn3;
					conn3.Open(Socket::TypeINET, 
						"localhost", 2003);
				#endif // !WIN32

				// Quick check that reconnections fail
				TEST_CHECK_THROWS(conn1.Open(Socket::TypeUNIX,
					"testfiles/srv2.sock");, 
					ServerException, SocketAlreadyOpen);

				// Stuff some data around
				std::vector<IOStream *> conns;
				conns.push_back(&conn1);

				#ifndef WIN32
					conns.push_back(&conn2);
					conns.push_back(&conn3);
				#endif // !WIN32

				Srv2TestConversations(conns);
				// Implicit close
			}

			#ifndef WIN32
				// HUP again
				TEST_THAT(HUPServer(pid));
				::sleep(1);
				TEST_THAT(ServerIsAlive(pid));
			#endif // !WIN32

			// Kill it
			TEST_THAT(KillServer(pid));
			::sleep(1);
			TEST_THAT(!ServerIsAlive(pid));

			#ifndef WIN32
				TestRemoteProcessMemLeaks("test-srv2.memleaks");
			#endif // !WIN32
		}
	}

	// Launch a test SSL server
	{
		std::string cmd = TEST_EXECUTABLE " --test-daemon-args=";
		cmd += test_args;
		cmd += " srv3 testfiles/srv3.conf";
		int pid = LaunchServer(cmd, "testfiles/srv3.pid");

		TEST_THAT(pid != -1 && pid != 0);

		if(pid > 0)
		{
			// Will it restart?
			TEST_THAT(ServerIsAlive(pid));

			#ifndef WIN32
				TEST_THAT(HUPServer(pid));
				::sleep(1);
				TEST_THAT(ServerIsAlive(pid));
			#endif

			// Make some connections
			{
				// SSL library
				SSLLib::Initialise();
			
				// Context first
				TLSContext context;
				context.Initialise(false /* client */,
						"testfiles/clientCerts.pem",
						"testfiles/clientPrivKey.pem",
						"testfiles/clientTrustedCAs.pem");

				SocketStreamTLS conn1;
				conn1.Open(context, Socket::TypeINET, "localhost", 2003);
				#ifndef WIN32
					SocketStreamTLS conn2;
					conn2.Open(context, Socket::TypeUNIX,
						"testfiles/srv3.sock");
					SocketStreamTLS conn3;
					conn3.Open(context, Socket::TypeINET,
						"localhost", 2003);
				#endif

				// Quick check that reconnections fail
				TEST_CHECK_THROWS(conn1.Open(context,
					Socket::TypeUNIX,
					"testfiles/srv3.sock");,
					ServerException, SocketAlreadyOpen);

				// Stuff some data around
				std::vector<IOStream *> conns;
				conns.push_back(&conn1);

				#ifndef WIN32
					conns.push_back(&conn2);
					conns.push_back(&conn3);
				#endif

				Srv2TestConversations(conns);
				// Implicit close
			}

			#ifndef WIN32
				// HUP again
				TEST_THAT(HUPServer(pid));
				::sleep(1);
				TEST_THAT(ServerIsAlive(pid));
			#endif

			// Kill it
			TEST_THAT(KillServer(pid));
			::sleep(1);
			TEST_THAT(!ServerIsAlive(pid));

			#ifndef WIN32
				TestRemoteProcessMemLeaks("test-srv3.memleaks");
			#endif
		}
	}
	
//protocolserver:
	// Launch a test protocol handling server
	{
		std::string cmd = TEST_EXECUTABLE " --test-daemon-args=";
		cmd += test_args;
		cmd += " srv4 testfiles/srv4.conf";
		int pid = LaunchServer(cmd, "testfiles/srv4.pid");

		TEST_THAT(pid != -1 && pid != 0);

		if(pid > 0)
		{
			::sleep(1);
			TEST_THAT(ServerIsAlive(pid));

			// Open a connection to it		
			std::auto_ptr<SocketStream> apConn(new SocketStream);
			#ifdef WIN32
				apConn->Open(Socket::TypeINET, "localhost", 2003);
			#else
				apConn->Open(Socket::TypeUNIX, "testfiles/srv4.sock");
			#endif
			
			// Create a protocol
			TestProtocolClient protocol(apConn);

			// Simple query
			{
				std::auto_ptr<TestProtocolSimpleReply> reply(protocol.QuerySimple(41));
				TEST_THAT(reply->GetValuePlusOne() == 42);
			}
			{
				std::auto_ptr<TestProtocolSimpleReply> reply(protocol.QuerySimple(809));
				TEST_THAT(reply->GetValuePlusOne() == 810);
			}
			
			BOX_INFO("Streams, twice, both uncertain and certain sizes");
			TestStreamReceive(protocol, 374, false);
			TestStreamReceive(protocol, 23983, true);
			TestStreamReceive(protocol, 12098, false);
			TestStreamReceive(protocol, 4342, true);
			
			BOX_INFO("Try to send a stream");
			{
				std::auto_ptr<CollectInBufferStream>
					s(new CollectInBufferStream());
				char buf[1663];
				s->Write(buf, sizeof(buf));
				s->SetForReading();
				std::auto_ptr<TestProtocolGetStream> reply(protocol.QuerySendStream(0x73654353298ffLL, 
					(std::auto_ptr<IOStream>)s));
				TEST_THAT(reply->GetStartingValue() == sizeof(buf));
			}

			BOX_INFO("Lots of simple queries");
			for(int q = 0; q < 514; q++)
			{
				std::auto_ptr<TestProtocolSimpleReply> reply(protocol.QuerySimple(q));
				TEST_THAT(reply->GetValuePlusOne() == (q+1));
			}

			BOX_INFO("Send a list of strings");
			{
				std::vector<std::string> strings;
				strings.push_back(std::string("test1"));
				strings.push_back(std::string("test2"));
				strings.push_back(std::string("test3"));
				std::auto_ptr<TestProtocolListsReply> reply(protocol.QueryLists(strings));
				TEST_THAT(reply->GetNumberOfStrings() == 3);
			}
			
			BOX_INFO("Send some mixed data");
			{
				std::auto_ptr<TestProtocolHello> reply(protocol.QueryHello(41,87,11,std::string("pingu")));
				TEST_THAT(reply->GetNumber32() == 12);
				TEST_THAT(reply->GetNumber16() == 89);
				TEST_THAT(reply->GetNumber8() == 22);
				TEST_THAT(reply->GetText() == "Hello world!");
			}

			BOX_INFO("Try to trigger an expected error");
			{
				TEST_CHECK_THROWS(protocol.QueryDeliberateError(),
					ConnectionException, Protocol_UnexpectedReply);
				int type, subtype;
				protocol.GetLastError(type, subtype);
				TEST_EQUAL(TestProtocolError::ErrorType, type);
				TEST_EQUAL(TestProtocolError::Err_DeliberateError, subtype);
			}

			BOX_INFO("Try to trigger an unexpected error");
			// ... by throwing an exception that isn't caught and handled by
			// HandleException:
			{
				TEST_CHECK_THROWS(protocol.QueryUnexpectedError(),
					ConnectionException, Protocol_UnexpectedReply);
				int type, subtype;
				protocol.GetLastError(type, subtype);
				TEST_EQUAL(TestProtocolError::ErrorType, type);
				TEST_EQUAL(-1, subtype);
			}

			// The unexpected exception should kill the server child process that we
			// connected to (except on Windows where the server does not fork a child),
			// so we cannot communicate with it any more:
			BOX_INFO("Try to end protocol (should fail as server is already dead)");
			{
				bool didthrow = false;
				HideExceptionMessageGuard hide;
				try
				{
					protocol.QueryQuit();
				}
				catch(ConnectionException &e)
				{
					if(e.GetSubType() == ConnectionException::SocketReadError ||
						e.GetSubType() == ConnectionException::SocketWriteError)
					{
						didthrow = true;
					}
					else
					{
						throw;
					}
				}
				if(!didthrow)
				{
					TEST_FAIL_WITH_MESSAGE("Didn't throw expected exception");
				}
			}

			// Kill the main server process:
			TEST_THAT(KillServer(pid));
			::sleep(1);
			TEST_THAT(!ServerIsAlive(pid));

			#ifndef WIN32
				TestRemoteProcessMemLeaks("test-srv4.memleaks");
			#endif
		}
	}

	return 0;
}

