// --------------------------------------------------------------------------
//
// File
//		Name:    testhttpserver.cpp
//		Purpose: Test code for HTTP server class
//		Created: 26/3/04
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>

#ifdef HAVE_SIGNAL_H
	#include <signal.h>
#endif

#include <openssl/hmac.h>

#include "autogen_HTTPException.h"
#include "HTTPRequest.h"
#include "HTTPResponse.h"
#include "HTTPServer.h"
#include "IOStreamGetLine.h"
#include "S3Client.h"
#include "S3Simulator.h"
#include "ServerControl.h"
#include "Test.h"
#include "decode.h"
#include "encode.h"

#include "MemLeakFindOn.h"

#define SHORT_TIMEOUT 5000
#define LONG_TIMEOUT 300000

class TestWebServer : public HTTPServer
{
public:
	TestWebServer();
	~TestWebServer();

	virtual void Handle(HTTPRequest &rRequest, HTTPResponse &rResponse);

};

// Build a nice HTML response, so this can also be tested neatly in a browser
void TestWebServer::Handle(HTTPRequest &rRequest, HTTPResponse &rResponse)
{
	// Test redirection mechanism
	if(rRequest.GetRequestURI() == "/redirect")
	{
		rResponse.SetAsRedirect("/redirected");
		return;
	}

	// Set a cookie?
	if(rRequest.GetRequestURI() == "/set-cookie")
	{
		rResponse.SetCookie("SetByServer", "Value1");
	}

	#define DEFAULT_RESPONSE_1 "<html>\n<head><title>TEST SERVER RESPONSE</title></head>\n<body><h1>Test response</h1>\n<p><b>URI:</b> "
	#define DEFAULT_RESPONSE_3 "</p>\n<p><b>Query string:</b> "
	#define DEFAULT_RESPONSE_4 "</p>\n<p><b>Method:</b> "
	#define DEFAULT_RESPONSE_5 "</p>\n<p><b>Decoded query:</b><br>"
	#define DEFAULT_RESPONSE_6 "</p>\n<p><b>Content type:</b> "
	#define DEFAULT_RESPONSE_7 "</p>\n<p><b>Content length:</b> "
	#define DEFAULT_RESPONSE_8 "</p>\n<p><b>Cookies:</b><br>\n"
	#define DEFAULT_RESPONSE_2 "</p>\n</body>\n</html>\n"

	rResponse.SetResponseCode(HTTPResponse::Code_OK);
	rResponse.SetContentType("text/html");
	rResponse.Write(DEFAULT_RESPONSE_1, sizeof(DEFAULT_RESPONSE_1) - 1);
	const std::string &ruri(rRequest.GetRequestURI());
	rResponse.Write(ruri.c_str(), ruri.size());
	rResponse.Write(DEFAULT_RESPONSE_3, sizeof(DEFAULT_RESPONSE_3) - 1);
	const std::string &rquery(rRequest.GetQueryString());
	rResponse.Write(rquery.c_str(), rquery.size());
	rResponse.Write(DEFAULT_RESPONSE_4, sizeof(DEFAULT_RESPONSE_4) - 1);
	{
		const char *m = "????";
		switch(rRequest.GetMethod())
		{
		case HTTPRequest::Method_GET: m = "GET "; break;
		case HTTPRequest::Method_HEAD: m = "HEAD"; break;
		case HTTPRequest::Method_POST: m = "POST"; break;
		default: m = "UNKNOWN";
		}
		rResponse.Write(m, 4);
	}
	rResponse.Write(DEFAULT_RESPONSE_5, sizeof(DEFAULT_RESPONSE_5) - 1);
	{
		const HTTPRequest::Query_t &rquery(rRequest.GetQuery());
		for(HTTPRequest::Query_t::const_iterator i(rquery.begin()); i != rquery.end(); ++i)
		{
			rResponse.Write("\nPARAM:", 7);
			rResponse.Write(i->first.c_str(), i->first.size());
			rResponse.Write("=", 1);
			rResponse.Write(i->second.c_str(), i->second.size());
			rResponse.Write("<br>\n", 4);
		}
	}
	rResponse.Write(DEFAULT_RESPONSE_6, sizeof(DEFAULT_RESPONSE_6) - 1);
	const std::string &rctype(rRequest.GetContentType());
	rResponse.Write(rctype.c_str(), rctype.size());
	rResponse.Write(DEFAULT_RESPONSE_7, sizeof(DEFAULT_RESPONSE_7) - 1);
	{
		char l[32];
		rResponse.Write(l, ::sprintf(l, "%d", rRequest.GetContentLength()));
	}
	rResponse.Write(DEFAULT_RESPONSE_8, sizeof(DEFAULT_RESPONSE_8) - 1);
	const HTTPRequest::CookieJar_t *pcookies = rRequest.GetCookies();
	if(pcookies != 0)
	{
		HTTPRequest::CookieJar_t::const_iterator i(pcookies->begin());
		for(; i != pcookies->end(); ++i)
		{
			char t[512];
			rResponse.Write(t, ::sprintf(t, "COOKIE:%s=%s<br>\n", i->first.c_str(), i->second.c_str()));
		}
	}
	rResponse.Write(DEFAULT_RESPONSE_2, sizeof(DEFAULT_RESPONSE_2) - 1);
}

TestWebServer::TestWebServer() {}
TestWebServer::~TestWebServer() {}

bool exercise_s3client(S3Client& client)
{
	bool success = true;

	HTTPResponse response = client.GetObject("/photos/puppy.jpg");
	TEST_EQUAL_OR(200, response.GetResponseCode(), success = false);
	std::string response_data((const char *)response.GetBuffer(),
		response.GetSize());
	TEST_EQUAL_OR("omgpuppies!\n", response_data, success = false);
	TEST_THAT_OR(!response.IsKeepAlive(), success = false);

	// make sure that assigning to HTTPResponse does clear stream
	response = client.GetObject("/photos/puppy.jpg");
	TEST_EQUAL_OR(200, response.GetResponseCode(), success = false);
	response_data = std::string((const char *)response.GetBuffer(),
		response.GetSize());
	TEST_EQUAL_OR("omgpuppies!\n", response_data, success = false);
	TEST_THAT_OR(!response.IsKeepAlive(), success = false);

	response = client.GetObject("/nonexist");
	TEST_EQUAL_OR(404, response.GetResponseCode(), success = false);
	TEST_THAT_OR(!response.IsKeepAlive(), success = false);

	FileStream fs("testfiles/testrequests.pl");
	response = client.PutObject("/newfile", fs);
	TEST_EQUAL_OR(200, response.GetResponseCode(), success = false);
	TEST_THAT_OR(!response.IsKeepAlive(), success = false);

	response = client.GetObject("/newfile");
	TEST_EQUAL_OR(200, response.GetResponseCode(), success = false);
	TEST_THAT_OR(fs.CompareWith(response), success = false);
	TEST_EQUAL_OR(0, ::unlink("testfiles/newfile"), success = false);

	return success;
}

int test(int argc, const char *argv[])
{
	if(argc >= 2 && ::strcmp(argv[1], "server") == 0)
	{
		// Run a server
		TestWebServer server;
		return server.Main("doesnotexist", argc - 1, argv + 1);
	}

	if(argc >= 2 && ::strcmp(argv[1], "s3server") == 0)
	{
		// Run a server
		S3Simulator server;
		return server.Main("doesnotexist", argc - 1, argv + 1);
	}

	TEST_THAT(system("rm -rf *.memleaks") == 0);

	// Test that HTTPRequest can be written to and read from a stream.
	{
		HTTPRequest request(HTTPRequest::Method_PUT, "/newfile");
		request.SetHostName("quotes.s3.amazonaws.com");
		// Write headers in lower case.
		request.AddHeader("date", "Wed, 01 Mar  2006 12:00:00 GMT");
		request.AddHeader("authorization",
			"AWS 0PN5J17HBGZHT7JJ3X82:XtMYZf0hdOo4TdPYQknZk0Lz7rw=");
		request.AddHeader("Content-Type", "text/plain");
		request.SetClientKeepAliveRequested(true);

		// Stream it to a CollectInBufferStream
		CollectInBufferStream request_buffer;

		// Because there isn't an HTTP server to respond to us, we can't use
		// SendWithStream, so just send the content after the request.
		request.SendHeaders(request_buffer, IOStream::TimeOutInfinite);
		FileStream fs("testfiles/testrequests.pl");
		fs.CopyStreamTo(request_buffer);

		request_buffer.SetForReading();

		IOStreamGetLine getLine(request_buffer);
		HTTPRequest request2;
		TEST_THAT(request2.Receive(getLine, IOStream::TimeOutInfinite));

		TEST_EQUAL(HTTPRequest::Method_PUT, request2.GetMethod());
		TEST_EQUAL("PUT", request2.GetMethodName());
		TEST_EQUAL("/newfile", request2.GetRequestURI());
		TEST_EQUAL("quotes.s3.amazonaws.com", request2.GetHostName());
		TEST_EQUAL(80, request2.GetHostPort());
		TEST_EQUAL("", request2.GetQueryString());
		TEST_EQUAL("text/plain", request2.GetContentType());
		// Content-Length was not known when the stream was sent, so it should
		// be unknown in the received stream too (certainly before it has all
		// been read!)
		TEST_EQUAL(-1, request2.GetContentLength());
		HTTPHeaders& headers(request2.GetHeaders());
		TEST_EQUAL("Wed, 01 Mar  2006 12:00:00 GMT",
			headers.GetHeaderValue("Date"));
		TEST_EQUAL("AWS 0PN5J17HBGZHT7JJ3X82:XtMYZf0hdOo4TdPYQknZk0Lz7rw=",
			headers.GetHeaderValue("Authorization"));
		TEST_THAT(request2.GetClientKeepAliveRequested());

		CollectInBufferStream request_data;
		request2.ReadContent(request_data, IOStream::TimeOutInfinite);
		TEST_EQUAL(fs.GetPosition(), request_data.GetPosition());
		request_data.SetForReading();
		fs.Seek(0, IOStream::SeekType_Absolute);
		TEST_THAT(fs.CompareWith(request_data, IOStream::TimeOutInfinite));
	}

	// Test that HTTPResponse can be written to and read from a stream.
	// TODO FIXME: we should stream the response instead of buffering it, on both
	// sides (send and receive).
	{
		// Stream it to a CollectInBufferStream
		CollectInBufferStream response_buffer;

		HTTPResponse response(&response_buffer);
		FileStream fs("testfiles/testrequests.pl");
		// Write headers in lower case.
		response.SetResponseCode(HTTPResponse::Code_OK);
		response.AddHeader("date", "Wed, 01 Mar  2006 12:00:00 GMT");
		response.AddHeader("authorization",
			"AWS 0PN5J17HBGZHT7JJ3X82:XtMYZf0hdOo4TdPYQknZk0Lz7rw=");
		response.AddHeader("content-type", "text/perl");
		fs.CopyStreamTo(response);
		response.Send();
		response_buffer.SetForReading();

		HTTPResponse response2;
		response2.Receive(response_buffer);

		TEST_EQUAL(200, response2.GetResponseCode());
		TEST_EQUAL("text/perl", response2.GetContentType());
		// Content-Length was not known when the stream was sent, so it should
		// be unknown in the received stream too (certainly before it has all
		// been read!)
		TEST_EQUAL(fs.GetPosition(), response2.GetContentLength());
		HTTPHeaders& headers(response2.GetHeaders());
		TEST_EQUAL("Wed, 01 Mar  2006 12:00:00 GMT",
			headers.GetHeaderValue("Date"));
		TEST_EQUAL("AWS 0PN5J17HBGZHT7JJ3X82:XtMYZf0hdOo4TdPYQknZk0Lz7rw=",
			headers.GetHeaderValue("Authorization"));

		CollectInBufferStream response_data;
		// request2.ReadContent(request_data, IOStream::TimeOutInfinite);
		response2.CopyStreamTo(response_data);
		TEST_EQUAL(fs.GetPosition(), response_data.GetPosition());
		response_data.SetForReading();
		fs.Seek(0, IOStream::SeekType_Absolute);
		TEST_THAT(fs.CompareWith(response_data, IOStream::TimeOutInfinite));
	}

	// Start the server
	int pid = StartDaemon(0, "./_test server testfiles/httpserver.conf",
		"testfiles/httpserver.pid");
	TEST_THAT_OR(pid > 0, return 1);

	// Run the request script
	TEST_THAT(::system("perl testfiles/testrequests.pl") == 0);

#ifdef ENABLE_KEEPALIVE_SUPPORT // incomplete, need chunked encoding support
	#ifndef WIN32
	signal(SIGPIPE, SIG_IGN);
	#endif

	SocketStream sock;
	sock.Open(Socket::TypeINET, "localhost", 1080);

	for (int i = 0; i < 4; i++)
	{
		HTTPRequest request(HTTPRequest::Method_GET,
			"/test-one/34/341s/234?p1=vOne&p2=vTwo");

		if (i < 2)
		{
			// first set of passes has keepalive off by default,
			// so when i == 1 the socket has already been closed
			// by the server, and we'll get -EPIPE when we try
			// to send the request.
			request.SetClientKeepAliveRequested(false);
		}
		else
		{
			request.SetClientKeepAliveRequested(true);
		}

		if (i == 1)
		{
			sleep(1); // need time for our process to realise
			// that the peer has died, otherwise no SIGPIPE :(
			TEST_CHECK_THROWS(
				request.Send(sock, SHORT_TIMEOUT),
				ConnectionException, SocketWriteError);
			sock.Close();
			sock.Open(Socket::TypeINET, "localhost", 1080);
			continue;
		}
		else
		{
			request.Send(sock, SHORT_TIMEOUT);
		}

		HTTPResponse response;
		response.Receive(sock, SHORT_TIMEOUT);

		TEST_THAT(response.GetResponseCode() == HTTPResponse::Code_OK);
		TEST_THAT(response.GetContentType() == "text/html");

		IOStreamGetLine getline(response);
		std::string line;

		TEST_THAT(getline.GetLine(line));
		TEST_EQUAL("<html>", line);
		TEST_THAT(getline.GetLine(line));
		TEST_EQUAL("<head><title>TEST SERVER RESPONSE</title></head>",
			line);
		TEST_THAT(getline.GetLine(line));
		TEST_EQUAL("<body><h1>Test response</h1>", line);
		TEST_THAT(getline.GetLine(line));
		TEST_EQUAL("<p><b>URI:</b> /test-one/34/341s/234</p>", line);
		TEST_THAT(getline.GetLine(line));
		TEST_EQUAL("<p><b>Query string:</b> p1=vOne&p2=vTwo</p>", line);
		TEST_THAT(getline.GetLine(line));
		TEST_EQUAL("<p><b>Method:</b> GET </p>", line);
		TEST_THAT(getline.GetLine(line));
		TEST_EQUAL("<p><b>Decoded query:</b><br>", line);
		TEST_THAT(getline.GetLine(line));
		TEST_EQUAL("PARAM:p1=vOne<br>", line);
		TEST_THAT(getline.GetLine(line));
		TEST_EQUAL("PARAM:p2=vTwo<br></p>", line);
		TEST_THAT(getline.GetLine(line));
		TEST_EQUAL("<p><b>Content type:</b> </p>", line);
		TEST_THAT(getline.GetLine(line));
		TEST_EQUAL("<p><b>Content length:</b> -1</p>", line);
		TEST_THAT(getline.GetLine(line));
		TEST_EQUAL("<p><b>Cookies:</b><br>", line);
		TEST_THAT(getline.GetLine(line));
		TEST_EQUAL("</p>", line);
		TEST_THAT(getline.GetLine(line));
		TEST_EQUAL("</body>", line);
		TEST_THAT(getline.GetLine(line));
		TEST_EQUAL("</html>", line);

		if(!response.IsKeepAlive())
		{
			BOX_TRACE("Server will close the connection, closing our end too.");
			sock.Close();
			sock.Open(Socket::TypeINET, "localhost", 1080);
		}
		else
		{
			BOX_TRACE("Server will keep the connection open for more requests.");
		}
	}

	sock.Close();
#endif // ENABLE_KEEPALIVE_SUPPORT

	// Kill it
	TEST_THAT(StopDaemon(pid, "testfiles/httpserver.pid",
		"generic-httpserver.memleaks", true));

	// This is the example from the Amazon S3 Developers Guide, page 31.
	// Correct, official signature should succeed, with lower-case headers.
	{
		// http://docs.amazonwebservices.com/AmazonS3/2006-03-01/RESTAuthentication.html
		HTTPRequest request(HTTPRequest::Method_GET, "/photos/puppy.jpg");
		request.SetHostName("johnsmith.s3.amazonaws.com");
		request.AddHeader("date", "Tue, 27 Mar 2007 19:36:42 +0000");
		request.AddHeader("authorization",
			"AWS 0PN5J17HBGZHT7JJ3X82:xXjDGYUmKxnwqr5KXNPGldn5LbA=");

		S3Simulator simulator;
		simulator.Configure("testfiles/s3simulator.conf");

		CollectInBufferStream response_buffer;
		HTTPResponse response(&response_buffer);

		simulator.Handle(request, response);
		TEST_EQUAL(200, response.GetResponseCode());

		std::string response_data((const char *)response.GetBuffer(),
			response.GetSize());
		TEST_EQUAL("omgpuppies!\n", response_data);
	}

	// Modified signature should fail.
	{
		// http://docs.amazonwebservices.com/AmazonS3/2006-03-01/RESTAuthentication.html
		HTTPRequest request(HTTPRequest::Method_GET, "/photos/puppy.jpg");
		request.SetHostName("johnsmith.s3.amazonaws.com");
		request.AddHeader("date", "Tue, 27 Mar 2007 19:36:42 +0000");
		request.AddHeader("authorization",
			"AWS 0PN5J17HBGZHT7JJ3X82:xXjDGYUmKxnwqr5KXNPGldn5LbB=");

		S3Simulator simulator;
		simulator.Configure("testfiles/s3simulator.conf");

		CollectInBufferStream response_buffer;
		HTTPResponse response(&response_buffer);

		simulator.Handle(request, response);
		TEST_EQUAL(401, response.GetResponseCode());

		std::string response_data((const char *)response.GetBuffer(),
			response.GetSize());
		TEST_EQUAL("<html><head>"
			"<title>Internal Server Error</title></head>\n"
			"<h1>Internal Server Error</h1>\n"
			"<p>An error, type Authentication Failed occured "
			"when processing the request.</p>"
			"<p>Please try again later.</p></body>\n"
			"</html>\n", response_data);
	}

	// S3Client tests with S3Simulator in-process server for debugging
	{
		S3Simulator simulator;
		simulator.Configure("testfiles/s3simulator.conf");
		S3Client client(&simulator, "johnsmith.s3.amazonaws.com",
			"0PN5J17HBGZHT7JJ3X82",
			"uV3F3YluFJax1cknvbcGwgjvx4QpvB+leU8dUj2o");
		TEST_THAT(exercise_s3client(client));
	}

	{
		HTTPRequest request(HTTPRequest::Method_PUT, "/newfile");
		request.SetHostName("quotes.s3.amazonaws.com");
		request.AddHeader("date", "Wed, 01 Mar  2006 12:00:00 GMT");
		request.AddHeader("authorization",
			"AWS 0PN5J17HBGZHT7JJ3X82:XtMYZf0hdOo4TdPYQknZk0Lz7rw=");
		// request.AddHeader("Content-Type", "text/plain");

		FileStream fs("testfiles/testrequests.pl");
		request.SetDataStream(&fs);
		request.SetForReading();

		CollectInBufferStream response_buffer;
		HTTPResponse response(&response_buffer);

		S3Simulator simulator;
		simulator.Configure("testfiles/s3simulator.conf");
		simulator.Handle(request, response);

		TEST_EQUAL(200, response.GetResponseCode());
		TEST_EQUAL("LriYPLdmOdAiIfgSm/F1YsViT1LW94/xUQxMsF7xiEb1a0wiIOIxl+zbwZ163pt7",
			response.GetHeaderValue("x-amz-id-2"));
		TEST_EQUAL("F2A8CCCA26B4B26D", response.GetHeaderValue("x-amz-request-id"));
		TEST_EQUAL("Wed, 01 Mar  2006 12:00:00 GMT", response.GetHeaderValue("Date"));
		TEST_EQUAL("Sun, 1 Jan 2006 12:00:00 GMT", response.GetHeaderValue("Last-Modified"));
		TEST_EQUAL("\"828ef3fdfa96f00ad9f27c383fc9ac7f\"", response.GetHeaderValue("ETag"));
		TEST_EQUAL("", response.GetContentType());
		TEST_EQUAL("AmazonS3", response.GetHeaderValue("Server"));
		TEST_EQUAL(0, response.GetSize());
		TEST_THAT(!response.IsKeepAlive());

		FileStream f1("testfiles/testrequests.pl");
		FileStream f2("testfiles/newfile");
		TEST_THAT(f1.CompareWith(f2));
		TEST_EQUAL(0, ::unlink("testfiles/newfile"));
	}

	// Start the S3Simulator server
	pid = StartDaemon(0, "./_test s3server testfiles/s3simulator.conf",
		"testfiles/s3simulator.pid");
	TEST_THAT_OR(pid > 0, return 1);

	// This is the example from the Amazon S3 Developers Guide, page 31
	{
		SocketStream sock;
		sock.Open(Socket::TypeINET, "localhost", 1080);

		HTTPRequest request(HTTPRequest::Method_GET, "/photos/puppy.jpg");
		request.SetHostName("johnsmith.s3.amazonaws.com");
		request.AddHeader("date", "Tue, 27 Mar 2007 19:36:42 +0000");
		request.AddHeader("authorization",
			"AWS 0PN5J17HBGZHT7JJ3X82:xXjDGYUmKxnwqr5KXNPGldn5LbA=");
		request.Send(sock, SHORT_TIMEOUT);

		HTTPResponse response;
		response.Receive(sock, SHORT_TIMEOUT);
		std::string value;
		TEST_EQUAL(200, response.GetResponseCode());
	}

	{
		SocketStream sock;
		sock.Open(Socket::TypeINET, "localhost", 1080);

		HTTPRequest request(HTTPRequest::Method_GET, "/nonexist");
		request.SetHostName("quotes.s3.amazonaws.com");
		request.AddHeader("Date", "Wed, 01 Mar  2006 12:00:00 GMT");
		request.AddHeader("Authorization", "AWS 0PN5J17HBGZHT7JJ3X82:0cSX/YPdtXua1aFFpYmH1tc0ajA=");
		request.SetClientKeepAliveRequested(true);
		request.Send(sock, SHORT_TIMEOUT);

		HTTPResponse response;
		response.Receive(sock, SHORT_TIMEOUT);
		std::string value;
		TEST_EQUAL(404, response.GetResponseCode());
		TEST_THAT(!response.IsKeepAlive());
	}

	#ifndef WIN32 // much harder to make files inaccessible on WIN32
	// Make file inaccessible, should cause server to return a 403 error,
	// unless of course the test is run as root :)
	{
		SocketStream sock;
		sock.Open(Socket::TypeINET, "localhost", 1080);

		TEST_THAT(chmod("testfiles/testrequests.pl", 0) == 0);
		HTTPRequest request(HTTPRequest::Method_GET,
			"/testrequests.pl");
		request.SetHostName("quotes.s3.amazonaws.com");
		request.AddHeader("Date", "Wed, 01 Mar  2006 12:00:00 GMT");
		request.AddHeader("Authorization", "AWS 0PN5J17HBGZHT7JJ3X82:qc1e8u8TVl2BpIxwZwsursIb8U8=");
		request.SetClientKeepAliveRequested(true);
		request.Send(sock, SHORT_TIMEOUT);

		HTTPResponse response;
		response.Receive(sock, SHORT_TIMEOUT);
		std::string value;
		TEST_EQUAL(403, response.GetResponseCode());
		TEST_THAT(chmod("testfiles/testrequests.pl", 0755) == 0);
	}
	#endif

	{
		SocketStream sock;
		sock.Open(Socket::TypeINET, "localhost", 1080);

		HTTPRequest request(HTTPRequest::Method_GET,
			"/testrequests.pl");
		request.SetHostName("quotes.s3.amazonaws.com");
		request.AddHeader("Date", "Wed, 01 Mar  2006 12:00:00 GMT");
		request.AddHeader("Authorization", "AWS 0PN5J17HBGZHT7JJ3X82:qc1e8u8TVl2BpIxwZwsursIb8U8=");
		request.SetClientKeepAliveRequested(true);
		request.Send(sock, SHORT_TIMEOUT);

		HTTPResponse response;
		response.Receive(sock, SHORT_TIMEOUT);
		std::string value;
		TEST_EQUAL(200, response.GetResponseCode());
		TEST_EQUAL("qBmKRcEWBBhH6XAqsKU/eg24V3jf/kWKN9dJip1L/FpbYr9FDy7wWFurfdQOEMcY", response.GetHeaderValue("x-amz-id-2"));
		TEST_EQUAL("F2A8CCCA26B4B26D", response.GetHeaderValue("x-amz-request-id"));
		TEST_EQUAL("Wed, 01 Mar  2006 12:00:00 GMT", response.GetHeaderValue("Date"));
		TEST_EQUAL("Sun, 1 Jan 2006 12:00:00 GMT", response.GetHeaderValue("Last-Modified"));
		TEST_EQUAL("\"828ef3fdfa96f00ad9f27c383fc9ac7f\"", response.GetHeaderValue("ETag"));
		TEST_EQUAL("text/plain", response.GetContentType());
		TEST_EQUAL("AmazonS3", response.GetHeaderValue("Server"));
		TEST_THAT(!response.IsKeepAlive());

		FileStream file("testfiles/testrequests.pl");
		TEST_THAT(file.CompareWith(response));
	}

	{
		SocketStream sock;
		sock.Open(Socket::TypeINET, "localhost", 1080);

		HTTPRequest request(HTTPRequest::Method_PUT,
			"/newfile");
		request.SetHostName("quotes.s3.amazonaws.com");
		request.AddHeader("Date", "Wed, 01 Mar  2006 12:00:00 GMT");
		request.AddHeader("Authorization", "AWS 0PN5J17HBGZHT7JJ3X82:kfY1m6V3zTufRy2kj92FpQGKz4M=");
		request.AddHeader("Content-Type", "text/plain");
		FileStream fs("testfiles/testrequests.pl");
		HTTPResponse response;
		request.SendWithStream(sock, LONG_TIMEOUT, &fs, response);
		std::string value;
		TEST_EQUAL(200, response.GetResponseCode());
		TEST_EQUAL("LriYPLdmOdAiIfgSm/F1YsViT1LW94/xUQxMsF7xiEb1a0wiIOIxl+zbwZ163pt7", response.GetHeaderValue("x-amz-id-2"));
		TEST_EQUAL("F2A8CCCA26B4B26D", response.GetHeaderValue("x-amz-request-id"));
		TEST_EQUAL("Wed, 01 Mar  2006 12:00:00 GMT", response.GetHeaderValue("Date"));
		TEST_EQUAL("Sun, 1 Jan 2006 12:00:00 GMT", response.GetHeaderValue("Last-Modified"));
		TEST_EQUAL("\"828ef3fdfa96f00ad9f27c383fc9ac7f\"", response.GetHeaderValue("ETag"));
		TEST_EQUAL("", response.GetContentType());
		TEST_EQUAL("AmazonS3", response.GetHeaderValue("Server"));
		TEST_EQUAL(0, response.GetSize());
		TEST_THAT(!response.IsKeepAlive());

		FileStream f1("testfiles/testrequests.pl");
		FileStream f2("testfiles/newfile");
		TEST_THAT(f1.CompareWith(f2));
	}

	// S3Client tests with S3Simulator daemon for realism
	{
		S3Client client("localhost", 1080, "0PN5J17HBGZHT7JJ3X82",
			"uV3F3YluFJax1cknvbcGwgjvx4QpvB+leU8dUj2o");
		TEST_THAT(exercise_s3client(client));
	}

	// Kill it
	TEST_THAT(StopDaemon(pid, "testfiles/s3simulator.pid",
		"s3simulator.memleaks", true));

	return 0;
}

