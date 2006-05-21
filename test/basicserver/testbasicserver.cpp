// --------------------------------------------------------------------------
//
// File
//		Name:    testbasicserver.cpp
//		Purpose: Test basic server classes
//		Created: 2003/07/29
//
// --------------------------------------------------------------------------


#include "Box.h"

#include <stdio.h>
#include <unistd.h>
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
#include "autogen_TestProtocolClient.h"
#include "autogen_TestProtocolServer.h"

#include "MemLeakFindOn.h"


#define SERVER_LISTEN_PORT	2003

// in ms
#define COMMS_READ_TIMEOUT					4
#define COMMS_SERVER_WAIT_BEFORE_REPLYING	40

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
	Sleep(COMMS_SERVER_WAIT_BEFORE_REPLYING * 1000);
#else
	struct timespec t;
	t.tv_sec = 0;
	t.tv_nsec = COMMS_SERVER_WAIT_BEFORE_REPLYING * 1000 * 1000;	// convert to ns
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
		std::string line;
		while(!getline.GetLine(line))
			;
		SocketStreamTLS &rtls = (SocketStreamTLS&)rStream;
		std::string line1("CONNECTED:");
		line1 += rtls.GetPeerCommonName();
		line1 += '\n';
		testservers_pause_before_reply();
		rStream.Write(line1.c_str(), line1.size());
	}

	while(!getline.IsEOF())
	{
		std::string line;
		while(!getline.GetLine(line))
			;
		if(line == "QUIT")
		{
			break;
		}
		if(line == "LARGEDATA")
		{
			{
				// Send lots of data
				char data[LARGE_DATA_BLOCK_SIZE];
				for(unsigned int y = 0; y < sizeof(data); y++)
				{
					data[y] = y & 0xff;
				}
				for(int s = 0; s < (LARGE_DATA_SIZE / LARGE_DATA_BLOCK_SIZE); ++s)
				{
					rStream.Write(data, sizeof(data));
				}
			}
			{
				// Receive lots of data
				char buf[1024];
				int total = 0;
				int r = 0;
				while(total < LARGE_DATA_SIZE && (r = rStream.Read(buf, sizeof(buf))) != 0)
				{
					total += r;
				}
				TEST_THAT(total == LARGE_DATA_SIZE);
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
	
	void Connection(SocketStream &rStream);
	
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
		SERVERSTREAM_VERIFY_SERVER_KEYS(0)	// no default addresses
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

void testserver::Connection(SocketStream &rStream)
{
	testservers_connection(rStream);
}

class testProtocolServer : public testserver
{
public:
	testProtocolServer() {}
	~testProtocolServer() {}

	void Connection(SocketStream &rStream);
	
	virtual const char *DaemonName() const
	{
		return "test-srv4";
	}
};

void testProtocolServer::Connection(SocketStream &rStream)
{
	TestProtocolServer server(rStream);
	TestContext context;
	server.DoServer(context);
}


class testTLSserver : public ServerTLS<SERVER_LISTEN_PORT>
{
public:
	testTLSserver() {}
	~testTLSserver() {}
	
	void Connection(SocketStreamTLS &rStream);
	
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
		SERVERTLS_VERIFY_SERVER_KEYS(0)	// no default listen addresses
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

void testTLSserver::Connection(SocketStreamTLS &rStream)
{
	testservers_connection(rStream);
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
		
		bool hadTimeout = false;
		if(typeid(*conns[c]) == typeid(SocketStreamTLS))
		{
			SocketStreamTLS *ptls = (SocketStreamTLS *)conns[c];
			printf("Connected to '%s'\n", ptls->GetPeerCommonName().c_str());
		
			// Send some data, any data, to get the first response.
			conns[c]->Write("Hello\n", 6);
		
			std::string line1;
			while(!getline[c]->GetLine(line1, false, COMMS_READ_TIMEOUT))
				hadTimeout = true;
			TEST_THAT(line1 == "CONNECTED:CLIENT");
			TEST_THAT(hadTimeout)
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
			while(!getline[c]->GetLine(rep, false, COMMS_READ_TIMEOUT))
				hadTimeout = true;
			TEST_THAT(rep == recieve[q]);
			TEST_THAT(hadTimeout)
		}
	}
	for(unsigned int c = 0; c < conns.size(); ++c)
	{
		conns[c]->Write("LARGEDATA\n", 10);
	}
	for(unsigned int c = 0; c < conns.size(); ++c)
	{
		// Receive lots of data
		char buf[1024];
		int total = 0;
		int r = 0;
		while(total < LARGE_DATA_SIZE && (r = conns[c]->Read(buf, sizeof(buf))) != 0)
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
			conns[c]->Write(data, sizeof(data));
		}
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
	std::auto_ptr<TestProtocolClientGetStream> reply(protocol.QueryGetStream(value, uncertainstream));
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
		int bytes = stream->Read(((char*)values) + bytesleft, sizeof(values) - bytesleft);
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
	// Server launching stuff
	if(argc >= 2)
	{
		if(strcmp(argv[1], "srv1") == 0)
		{
			// Run very basic daemon
			basicdaemon daemon;
			return daemon.Main("doesnotexist", argc - 1, argv + 1);
		}
		else if(strcmp(argv[1], "srv2") == 0)
		{
			// Run daemon which accepts connections
			testserver daemon;
			return daemon.Main("doesnotexist", argc - 1, argv + 1);
		}		
		else if(strcmp(argv[1], "srv3") == 0)
		{
			testTLSserver daemon;
			return daemon.Main("doesnotexist", argc - 1, argv + 1);
		}
		else if(strcmp(argv[1], "srv4") == 0)
		{
			testProtocolServer daemon;
			return daemon.Main("doesnotexist", argc - 1, argv + 1);
		}
	}

//printf("SKIPPING TESTS------------------------\n");
//goto protocolserver;

	// Launch a basic server
	{
		int pid = LaunchServer("./test srv1 testfiles/srv1.conf", "testfiles/srv1.pid");
		TEST_THAT(pid != -1 && pid != 0);
		if(pid > 0)
		{
			// Check that it's written the expected file
			TEST_THAT(TestFileExists("testfiles/srv1.test1"));
			TEST_THAT(ServerIsAlive(pid));
			// Move the config file over
			TEST_THAT(::rename("testfiles/srv1b.conf", "testfiles/srv1.conf") != -1);
			// Get it to reread the config file
			TEST_THAT(HUPServer(pid));
			::sleep(1);
			TEST_THAT(ServerIsAlive(pid));
			// Check that new file exists
			TEST_THAT(TestFileExists("testfiles/srv1.test2"));
			// Kill it off
			TEST_THAT(KillServer(pid));
			TestRemoteProcessMemLeaks("generic-daemon.memleaks");
		}
	}
	
	// Launch a test forking server
	{
		int pid = LaunchServer("./test srv2 testfiles/srv2.conf", "testfiles/srv2.pid");
		TEST_THAT(pid != -1 && pid != 0);
		if(pid > 0)
		{
			// Will it restart?
			TEST_THAT(ServerIsAlive(pid));
			TEST_THAT(HUPServer(pid));
			::sleep(1);
			TEST_THAT(ServerIsAlive(pid));
			// Make some connections
			{
				SocketStream conn1;
				conn1.Open(Socket::TypeINET, "localhost", 2003);
				SocketStream conn2;
				conn2.Open(Socket::TypeUNIX, "testfiles/srv2.sock");
				SocketStream conn3;
				conn3.Open(Socket::TypeINET, "localhost", 2003);
				// Quick check that reconnections fail
				TEST_CHECK_THROWS(conn1.Open(Socket::TypeUNIX, "testfiles/srv2.sock");, ServerException, SocketAlreadyOpen);
				// Stuff some data around
				std::vector<IOStream *> conns;
				conns.push_back(&conn1);
				conns.push_back(&conn2);
				conns.push_back(&conn3);
				Srv2TestConversations(conns);
				// Implicit close
			}
			// HUP again
			TEST_THAT(HUPServer(pid));
			::sleep(1);
			TEST_THAT(ServerIsAlive(pid));
			// Kill it
			TEST_THAT(KillServer(pid));
			::sleep(1);
			TEST_THAT(!ServerIsAlive(pid));
			TestRemoteProcessMemLeaks("test-srv2.memleaks");
		}
	}

	// Launch a test SSL server
	{
		int pid = LaunchServer("./test srv3 testfiles/srv3.conf", "testfiles/srv3.pid");
		TEST_THAT(pid != -1 && pid != 0);
		if(pid > 0)
		{
			// Will it restart?
			TEST_THAT(ServerIsAlive(pid));
			TEST_THAT(HUPServer(pid));
			::sleep(1);
			TEST_THAT(ServerIsAlive(pid));
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
				SocketStreamTLS conn2;
				conn2.Open(context, Socket::TypeUNIX, "testfiles/srv3.sock");
				SocketStreamTLS conn3;
				conn3.Open(context, Socket::TypeINET, "localhost", 2003);
				// Quick check that reconnections fail
				TEST_CHECK_THROWS(conn1.Open(context, Socket::TypeUNIX, "testfiles/srv3.sock");, ServerException, SocketAlreadyOpen);
				// Stuff some data around
				std::vector<IOStream *> conns;
				conns.push_back(&conn1);
				conns.push_back(&conn2);
				conns.push_back(&conn3);
				Srv2TestConversations(conns);
				// Implicit close
			}
			// HUP again
			TEST_THAT(HUPServer(pid));
			::sleep(1);
			TEST_THAT(ServerIsAlive(pid));
			// Kill it
			TEST_THAT(KillServer(pid));
			::sleep(1);
			TEST_THAT(!ServerIsAlive(pid));
			TestRemoteProcessMemLeaks("test-srv3.memleaks");
		}
	}
	
//protocolserver:
	// Launch a test protocol handling server
	{
		int pid = LaunchServer("./test srv4 testfiles/srv4.conf", "testfiles/srv4.pid");
		TEST_THAT(pid != -1 && pid != 0);
		if(pid > 0)
		{
			::sleep(1);
			TEST_THAT(ServerIsAlive(pid));

			// Open a connection to it		
			SocketStream conn;
			conn.Open(Socket::TypeUNIX, "testfiles/srv4.sock");
			
			// Create a protocol
			TestProtocolClient protocol(conn);

			// Simple query
			{
				std::auto_ptr<TestProtocolClientSimpleReply> reply(protocol.QuerySimple(41));
				TEST_THAT(reply->GetValuePlusOne() == 42);
			}
			{
				std::auto_ptr<TestProtocolClientSimpleReply> reply(protocol.QuerySimple(809));
				TEST_THAT(reply->GetValuePlusOne() == 810);
			}
			
			// Streams, twice, both uncertain and certain sizes
			TestStreamReceive(protocol, 374, false);
			TestStreamReceive(protocol, 23983, true);
			TestStreamReceive(protocol, 12098, false);
			TestStreamReceive(protocol, 4342, true);
			
			// Try to send a stream
			{
				CollectInBufferStream s;
				char buf[1663];
				s.Write(buf, sizeof(buf));
				s.SetForReading();
				std::auto_ptr<TestProtocolClientGetStream> reply(protocol.QuerySendStream(0x73654353298ffLL, s));
				TEST_THAT(reply->GetStartingValue() == sizeof(buf));
			}

			// Lots of simple queries
			for(int q = 0; q < 514; q++)
			{
				std::auto_ptr<TestProtocolClientSimpleReply> reply(protocol.QuerySimple(q));
				TEST_THAT(reply->GetValuePlusOne() == (q+1));
			}
			// Send a list of strings to it
			{
				std::vector<std::string> strings;
				strings.push_back(std::string("test1"));
				strings.push_back(std::string("test2"));
				strings.push_back(std::string("test3"));
				std::auto_ptr<TestProtocolClientListsReply> reply(protocol.QueryLists(strings));
				TEST_THAT(reply->GetNumberOfStrings() == 3);
			}
			
			// And another
			{
				std::auto_ptr<TestProtocolClientHello> reply(protocol.QueryHello(41,87,11,std::string("pingu")));
				TEST_THAT(reply->GetNumber32() == 12);
				TEST_THAT(reply->GetNumber16() == 89);
				TEST_THAT(reply->GetNumber8() == 22);
				TEST_THAT(reply->GetText() == "Hello world!");
			}
		
			// Quit query to finish
			protocol.QueryQuit();
		
			// Kill it
			TEST_THAT(KillServer(pid));
			::sleep(1);
			TEST_THAT(!ServerIsAlive(pid));
			TestRemoteProcessMemLeaks("test-srv4.memleaks");
		}
	}

	return 0;
}

