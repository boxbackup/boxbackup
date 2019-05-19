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
#include <time.h>

#include <typeinfo>

#include "CollectInBufferStream.h"
#include "Configuration.h"
#include "Daemon.h"
#include "IOStreamGetLine.h"
#include "ServerControl.h"
#include "ServerStream.h"
#include "ServerTLS.h"
#include "SocketStream.h"
#include "Test.h"
#include "TestContext.h"
#include "autogen_TestProtocol.h"

#include "MemLeakFindOn.h"

#define SERVER_LISTEN_PORT	2003

// in ms
#define COMMS_READ_TIMEOUT					4
#define COMMS_SERVER_WAIT_BEFORE_REPLYING	40
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
		while(!getline.GetLine(line, false, SHORT_TIMEOUT))
			;
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

	static ConfigurationVerifyKey root_keys[] =
	{
		ssl_security_level_key,
	};

	static ConfigurationVerify verify =
	{
		"root", /* mName */
		verifyserver, /* mpSubConfigurations */
		root_keys, // mpKeys
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

bool test_security_level(int cert_level, int test_level, bool expect_failure_on_connect = false)
{
	int old_num_failures = num_failures;

	// Context first
	TLSContext context;
	if(cert_level == 0)
	{
		context.Initialise(false /* client */,
			"testfiles/clientCerts.pem",
			"testfiles/clientPrivKey.pem",
			"testfiles/clientTrustedCAs.pem",
			test_level); // SecurityLevel
	}
	else if(cert_level == 1)
	{
		context.Initialise(false /* client */,
			"testfiles/seclevel2-sha1/ca/clients/1234567-cert.pem",
			"testfiles/seclevel2-sha1/bbackupd/1234567-key.pem",
			"testfiles/seclevel2-sha1/ca/roots/serverCA.pem",
			test_level); // SecurityLevel
	}
	else if(cert_level == 2)
	{
		context.Initialise(false /* client */,
			"testfiles/seclevel2-sha256/ca/clients/1234567-cert.pem",
			"testfiles/seclevel2-sha256/bbackupd/1234567-key.pem",
			"testfiles/seclevel2-sha256/ca/roots/serverCA.pem",
			test_level); // SecurityLevel
	}
	else
	{
		TEST_FAIL_WITH_MESSAGE("No certificates generated for level " << cert_level);
		return false;
	}

	SocketStreamTLS conn;

	if(expect_failure_on_connect)
	{
		TEST_CHECK_THROWS(
			conn.Open(context, Socket::TypeINET, "localhost", 2003),
			ConnectionException, TLSPeerWeakCertificate);
	}
	else
	{
		conn.Open(context, Socket::TypeINET, "localhost", 2003);
	}

	return (num_failures == old_num_failures); // no new failures -> good
}

// Test the certificates that were distributed with the Box Backup source since ancient times,
// which have only 1024-bit keys, and thus fail with "ee key too small".
bool test_ancient_certificates()
{
	int old_num_failures = num_failures;

	// Level -1 (allow weaker, with warning) should pass with any certificates:
	TEST_THAT(test_security_level(0, -1)); // cert_level, test_level

	// We do not test level 0 (system-wide default) because the system
	// may have it set high, and our old certificate will not be usable
	// in that case, and the user has no way to fix that, so it's not a
	// useful test.

	// Level 1 (allow weaker, without a warning) should pass with any certificates:
	TEST_THAT(test_security_level(0, 1)); // cert_level, test_level

#ifdef HAVE_SSL_CTX_SET_SECURITY_LEVEL
	// Level 2 (disallow weaker, without a warning) should NOT pass with old certificates:
	TEST_CHECK_THROWS(
		test_security_level(0, 2), // cert_level, test_level
		ServerException, TLSServerWeakCertificate);
#else
	// We have no way to increase the security level, so it should still pass:
	test_security_level(0, 2); // cert_level, test_level
#endif

	return (num_failures == old_num_failures); // no new failures -> good
}

// Test a set of more recent certificates, which have a longer key but are signed using the SHA1
// algorithm instead of SHA256, which fail with "ca md too weak" instead.
bool test_old_certificates()
{
	int old_num_failures = num_failures;

	// Level -1 (allow weaker, with warning) should pass with any certificates:
	TEST_THAT(test_security_level(1, -1)); // cert_level, test_level

	// We do not test level 0 (system-wide default) because the system
	// may have it set high, and our old certificate will not be usable
	// in that case, and the user has no way to fix that, so it's not a
	// useful test.

	// Level 1 (allow weaker, without a warning) should pass with any certificates:
	TEST_THAT(test_security_level(1, 1)); // cert_level, test_level

#ifdef HAVE_SSL_CTX_SET_SECURITY_LEVEL
	// Level 2 (disallow weaker, without a warning) should NOT pass with old certificates:
	TEST_CHECK_THROWS(
		test_security_level(1, 2), // cert_level, test_level
		ServerException, TLSServerWeakCertificate);
#else
	// We have no way to increase the security level, so it should still pass:
	test_security_level(1, 2); // cert_level, test_level
#endif

	return (num_failures == old_num_failures); // no new failures -> good
}


bool test_new_certificates(bool expect_failure_level_2)
{
	int old_num_failures = num_failures;

	// Level -1 (allow weaker, with warning) should pass with any certificates:
	TEST_THAT(test_security_level(2, -1)); // cert_level, test_level

	// Level 0 (system dependent). This will fail if the user (or their
	// distro) sets the system-wide security level very high. We check
	// this because *we* may need to update Box Backup if this happens
	// again, as it did when Debian increased the default level.
	// Newly generated certificates may need to be strengthened.
	// And we may need to update the documentation.
	TEST_THAT(test_security_level(2, 0)); // cert_level, test_level

	// Level 1 (allow weaker, without a warning) should pass with any certificates:
	TEST_THAT(test_security_level(2, 1)); // cert_level, test_level

#ifdef HAVE_SSL_CTX_SET_SECURITY_LEVEL
	// Level 2 (disallow weaker, without a warning) should pass with new certificates,
	// but might fail to connect to a peer with weak (insecure) certificates:
	TEST_THAT(test_security_level(2, 2, expect_failure_level_2));
	// cert_level, test_level, expect_failure
#else
	// We have no way to increase the security level, so it should not fail to connect to a
	// daemon with weak certificates:
	test_security_level(2, 2, false); // cert_level, test_level, expect_failure
#endif

	return (num_failures == old_num_failures); // no new failures -> good
}


int test(int argc, const char *argv[])
{
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
				TEST_THAT(::unlink("testfiles"
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
				// SecurityLevel == -1 by default (old security + warnings)

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

			// Try testing with different security levels, check that the behaviour is
			// as documented at:
			// https://github.com/boxbackup/boxbackup/wiki/WeakSSLCertificates
			TEST_THAT(test_ancient_certificates());

			// Kill it
			TEST_THAT(KillServer(pid));
			::sleep(1);
			TEST_THAT(!ServerIsAlive(pid));

			#ifndef WIN32
				TestRemoteProcessMemLeaks("test-srv3.memleaks");
			#endif
		}

		cmd = TEST_EXECUTABLE " --test-daemon-args=";
		cmd += test_args;
		cmd += " srv3 testfiles/srv3-seclevel2-sha1.conf";
		pid = LaunchServer(cmd, "testfiles/srv3.pid");

		TEST_THAT(pid != -1 && pid != 0);
		TEST_THAT(test_old_certificates());
		TEST_THAT(KillServer(pid));

		cmd = TEST_EXECUTABLE " --test-daemon-args=";
		cmd += test_args;
		cmd += " srv3 testfiles/srv3-seclevel2-sha256.conf";
		pid = LaunchServer(cmd, "testfiles/srv3.pid");

		TEST_THAT(pid != -1 && pid != 0);
		TEST_THAT(test_new_certificates(false)); // !expect_failure_level_2
		TEST_THAT(KillServer(pid));

		// Start a daemon using old, insecure certificates. We should get an error when we
		// try to connect to it:

		cmd = TEST_EXECUTABLE " --test-daemon-args=";
		cmd += test_args;
		cmd += " srv3 testfiles/srv3-insecure-daemon.conf";
		pid = LaunchServer(cmd, "testfiles/srv3.pid");

		TEST_THAT(pid != -1 && pid != 0);
		TEST_THAT(test_new_certificates(true)); // expect_failure_level_2
		TEST_THAT(KillServer(pid));
	}
	
//protocolserver:
	// Launch a test protocol handling server
	{
		std::string cmd = TEST_EXECUTABLE " --test-daemon-args=";
		cmd += test_args;
		cmd += " srv4 testfiles/srv4-seclevel1.conf";
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
			
			// Streams, twice, both uncertain and certain sizes
			TestStreamReceive(protocol, 374, false);
			TestStreamReceive(protocol, 23983, true);
			TestStreamReceive(protocol, 12098, false);
			TestStreamReceive(protocol, 4342, true);
			
			// Try to send a stream
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

			// Lots of simple queries
			for(int q = 0; q < 514; q++)
			{
				std::auto_ptr<TestProtocolSimpleReply> reply(protocol.QuerySimple(q));
				TEST_THAT(reply->GetValuePlusOne() == (q+1));
			}
			// Send a list of strings to it
			{
				std::vector<std::string> strings;
				strings.push_back(std::string("test1"));
				strings.push_back(std::string("test2"));
				strings.push_back(std::string("test3"));
				std::auto_ptr<TestProtocolListsReply> reply(protocol.QueryLists(strings));
				TEST_THAT(reply->GetNumberOfStrings() == 3);
			}
			
			// And another
			{
				std::auto_ptr<TestProtocolHello> reply(protocol.QueryHello(41,87,11,std::string("pingu")));
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

			#ifndef WIN32
				TestRemoteProcessMemLeaks("test-srv4.memleaks");
			#endif
		}
	}

	return 0;
}

