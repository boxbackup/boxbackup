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

#include <openssl/x509.h>
#include <openssl/hmac.h>

#include "autogen_HTTPException.h"
#include "HTTPQueryDecoder.h"
#include "HTTPRequest.h"
#include "HTTPResponse.h"
#include "HTTPServer.h"
#include "HTTPTest.h"
#include "IOStreamGetLine.h"
#include "MD5Digest.h"
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
	TestWebServer()
	: HTTPServer(LONG_TIMEOUT)
	{ }
	~TestWebServer() { }

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

bool exercise_s3client(S3Client& client)
{
	int num_failures_initial = num_failures;

	HTTPResponse response = client.GetObject("/photos/puppy.jpg");
	TEST_EQUAL(200, response.GetResponseCode());
	std::string response_data((const char *)response.GetBuffer(),
		response.GetSize());
	TEST_EQUAL("omgpuppies!\n", response_data);
	TEST_THAT(!response.IsKeepAlive());

	// make sure that assigning to HTTPResponse does clear stream
	response = client.GetObject("/photos/puppy.jpg");
	TEST_EQUAL(200, response.GetResponseCode());
	response_data = std::string((const char *)response.GetBuffer(),
		response.GetSize());
	TEST_EQUAL("omgpuppies!\n", response_data);
	TEST_THAT(!response.IsKeepAlive());

	response = client.GetObject("/nonexist");
	TEST_EQUAL(404, response.GetResponseCode());
	TEST_THAT(!response.IsKeepAlive());

	// Test is successful if the number of failures has not increased.
	return (num_failures == num_failures_initial);
}

// http://docs.aws.amazon.com/AmazonSimpleDB/latest/DeveloperGuide/HMACAuth.html
std::string calculate_s3_signature(const HTTPRequest& request,
	const std::string& aws_secret_access_key)
{
	// This code is very similar to that in S3Client::FinishAndSendRequest.
	// TODO FIXME: factor out the common parts.

	std::ostringstream buffer_to_sign;
	buffer_to_sign << request.GetMethodName() << "\n" <<
		request.GetHeaders().GetHeaderValue("Content-MD5",
			false) << "\n" << // !required
		request.GetContentType() << "\n" <<
		request.GetHeaders().GetHeaderValue("Date",
			true) << "\n"; // required

	// TODO FIXME: add support for X-Amz headers (S3 DG page 38)

	std::string bucket;
	std::string host_header = request.GetHeaders().GetHeaderValue("Host",
		true); // required
	std::string s3suffix = ".s3.amazonaws.com";
	if(host_header.size() > s3suffix.size())
	{
		std::string suffix = host_header.substr(host_header.size() -
			s3suffix.size(), s3suffix.size());
		if (suffix == s3suffix)
		{
			bucket = "/" + host_header.substr(0, host_header.size() -
				s3suffix.size());
		}
	}

	buffer_to_sign << bucket << request.GetRequestURI();

	// TODO FIXME: add support for sub-resources. S3 DG page 36.

	// Thanks to https://gist.github.com/tsupo/112188:
	unsigned int digest_size;
	unsigned char digest_buffer[EVP_MAX_MD_SIZE];
	std::string string_to_sign = buffer_to_sign.str();

	HMAC(EVP_sha1(),
		aws_secret_access_key.c_str(), aws_secret_access_key.size(),
		(const unsigned char *)string_to_sign.c_str(), string_to_sign.size(),
		digest_buffer, &digest_size);

	base64::encoder encoder;
	std::string digest((const char *)digest_buffer, digest_size);
	std::string auth_code = encoder.encode(digest);

	if (auth_code[auth_code.size() - 1] == '\n')
	{
		auth_code = auth_code.substr(0, auth_code.size() - 1);
	}

	return auth_code;
}

bool send_and_receive(HTTPRequest& request, HTTPResponse& response,
	int expected_status_code = 200)
{
	SocketStream sock;
	sock.Open(Socket::TypeINET, "localhost", 1080);
	request.Send(sock, LONG_TIMEOUT);

	response.Reset();
	response.Receive(sock, LONG_TIMEOUT);
	std::string response_data((const char *)response.GetBuffer(),
		response.GetSize());
	TEST_EQUAL_LINE(expected_status_code, response.GetResponseCode(),
		response_data);
	return (response.GetResponseCode() == expected_status_code);
}

#define EXAMPLE_S3_ACCESS_KEY "0PN5J17HBGZHT7JJ3X82"
#define EXAMPLE_S3_SECRET_KEY "uV3F3YluFJax1cknvbcGwgjvx4QpvB+leU8dUj2o"

bool test_httpserver()
{
	SETUP();

	{
		FileStream fs("testfiles/dsfdsfs98.fd");
		MD5DigestStream digester;
		fs.CopyStreamTo(digester);
		fs.Seek(0, IOStream::SeekType_Absolute);
		digester.Close();
		std::string digest = digester.DigestAsString();
		TEST_EQUAL("dc3b8c5e57e71d31a0a9d7cbeee2e011", digest);
	}

	// Test that HTTPRequest with parameters is encoded correctly
	{
		HTTPRequest request(HTTPRequest::Method_GET, "/newfile");
		CollectInBufferStream request_buffer;
		request.SendHeaders(request_buffer, IOStream::TimeOutInfinite);
		request_buffer.SetForReading();

		std::string request_str((const char *)request_buffer.GetBuffer(),
			request_buffer.GetSize());
		const std::string expected_str("GET /newfile HTTP/1.1\r\nConnection: close\r\n\r\n");
		TEST_EQUAL(expected_str, request_str);

		request.AddParameter("foo", "Bar");
		request_buffer.Reset();
		request.SendHeaders(request_buffer, IOStream::TimeOutInfinite);
		request_str = std::string((const char *)request_buffer.GetBuffer(),
			request_buffer.GetSize());
		TEST_EQUAL("GET /newfile?foo=Bar HTTP/1.1\r\nConnection: close\r\n\r\n", request_str);

		request.AddParameter("foo", "baz");
		request_buffer.Reset();
		request.SendHeaders(request_buffer, IOStream::TimeOutInfinite);
		request_str = std::string((const char *)request_buffer.GetBuffer(),
			request_buffer.GetSize());
		TEST_EQUAL("GET /newfile?foo=Bar&foo=baz HTTP/1.1\r\nConnection: close\r\n\r\n", request_str);

		request.SetParameter("whee", "bonk");
		request_buffer.Reset();
		request.SendHeaders(request_buffer, IOStream::TimeOutInfinite);
		request_str = std::string((const char *)request_buffer.GetBuffer(),
			request_buffer.GetSize());
		TEST_EQUAL("GET /newfile?foo=Bar&foo=baz&whee=bonk HTTP/1.1\r\nConnection: close\r\n\r\n", request_str);

		request.SetParameter("foo", "bolt");
		request_buffer.Reset();
		request.SendHeaders(request_buffer, IOStream::TimeOutInfinite);
		request_str = std::string((const char *)request_buffer.GetBuffer(),
			request_buffer.GetSize());
		TEST_EQUAL("GET /newfile?foo=bolt&whee=bonk HTTP/1.1\r\nConnection: close\r\n\r\n", request_str);

		HTTPRequest newreq = request;
		TEST_EQUAL("bolt", newreq.GetParameterString("foo"));
		TEST_EQUAL("bonk", newreq.GetParameterString("whee"));
		TEST_EQUAL("blue", newreq.GetParameterString("colour", "blue"));
		TEST_CHECK_THROWS(newreq.GetParameterString("colour"), HTTPException,
			ParameterNotFound);
	}

	// Test that HTTPRequest can be written to and read from a stream.
	{
		HTTPRequest request(HTTPRequest::Method_PUT, "/newfile");
		request.SetHostName("quotes.s3.amazonaws.com");
		// Write headers in lower case.
		request.AddHeader("date", "Wed, 01 Mar  2006 12:00:00 GMT");
		request.AddHeader("authorization",
			"AWS " EXAMPLE_S3_ACCESS_KEY ":XtMYZf0hdOo4TdPYQknZk0Lz7rw=");
		request.AddHeader("Content-Type", "text/plain");
		request.SetClientKeepAliveRequested(true);

		// First stream just the headers into a CollectInBufferStream, and check the
		// exact contents written:
		CollectInBufferStream request_buffer;
		request.SendHeaders(request_buffer, IOStream::TimeOutInfinite);
		request_buffer.SetForReading();
		const std::string request_str((const char *)request_buffer.GetBuffer(),
			request_buffer.GetSize());
		const std::string expected_str(
			"PUT /newfile HTTP/1.1\r\n"
			"Content-Type: text/plain\r\n"
			"Host: quotes.s3.amazonaws.com\r\n"
			"Connection: keep-alive\r\n"
			"date: Wed, 01 Mar  2006 12:00:00 GMT\r\n"
			"authorization: AWS " EXAMPLE_S3_ACCESS_KEY ":XtMYZf0hdOo4TdPYQknZk0Lz7rw=\r\n"
			"\r\n");
		TEST_EQUAL(expected_str, request_str);

		// Now stream the entire request into the CollectInBufferStream. Because there
		// isn't an HTTP server to respond to us, we can't use SendWithStream, so just
		// send the headers and then the content separately:
		request_buffer.Reset();
		request.SendHeaders(request_buffer, IOStream::TimeOutInfinite);
		FileStream fs("testfiles/photos/puppy.jpg");
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
		const HTTPHeaders& headers(request2.GetHeaders());
		TEST_EQUAL("Wed, 01 Mar  2006 12:00:00 GMT",
			headers.GetHeaderValue("Date"));
		TEST_EQUAL("AWS " EXAMPLE_S3_ACCESS_KEY ":XtMYZf0hdOo4TdPYQknZk0Lz7rw=",
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
		FileStream fs("testfiles/photos/puppy.jpg");
		// Write headers in lower case.
		response.SetResponseCode(HTTPResponse::Code_OK);
		response.AddHeader("date", "Wed, 01 Mar  2006 12:00:00 GMT");
		response.AddHeader("authorization",
			"AWS " EXAMPLE_S3_ACCESS_KEY ":XtMYZf0hdOo4TdPYQknZk0Lz7rw=");
		response.AddHeader("content-type", "text/perl");
		fs.CopyStreamTo(response);
		response.Send();
		response_buffer.SetForReading();

		HTTPResponse response2;
		response2.Receive(response_buffer);

		TEST_EQUAL(200, response2.GetResponseCode());
		TEST_EQUAL("text/perl", response2.GetContentType());

		// TODO FIXME: Content-Length was not known when the stream was sent,
		// so it should be unknown in the received stream too (certainly before
		// it has all been read!) This is currently wrong because we read the
		// entire response into memory immediately.
		TEST_EQUAL(fs.GetPosition(), response2.GetContentLength());

		HTTPHeaders& headers(response2.GetHeaders());
		TEST_EQUAL("Wed, 01 Mar  2006 12:00:00 GMT",
			headers.GetHeaderValue("Date"));
		TEST_EQUAL("AWS " EXAMPLE_S3_ACCESS_KEY ":XtMYZf0hdOo4TdPYQknZk0Lz7rw=",
			headers.GetHeaderValue("Authorization"));

		CollectInBufferStream response_data;
		// request2.ReadContent(request_data, IOStream::TimeOutInfinite);
		response2.CopyStreamTo(response_data);
		TEST_EQUAL(fs.GetPosition(), response_data.GetPosition());
		response_data.SetForReading();
		fs.Seek(0, IOStream::SeekType_Absolute);
		TEST_THAT(fs.CompareWith(response_data, IOStream::TimeOutInfinite));
	}

#ifndef WIN32
	TEST_THAT(system("rm -rf *.memleaks") == 0);
#endif

	// Start the server
	int pid = StartDaemon(0, TEST_EXECUTABLE " server testfiles/httpserver.conf",
		"testfiles/httpserver.pid");
	TEST_THAT_OR(pid > 0, return 1);

	// Run the request script
	TEST_THAT(::system("perl testfiles/testrequests.pl") == 0);

	#ifndef WIN32
	signal(SIGPIPE, SIG_IGN);
	#endif

#ifdef ENABLE_KEEPALIVE_SUPPORT // incomplete, need chunked encoding support
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

	// Copy testfiles/puppy.jpg to testfiles/store/photos/puppy.jpg
	{
		TEST_THAT(::mkdir("testfiles/store/photos", 0755) == 0);
		FileStream in("testfiles/puppy.jpg", O_RDONLY);
		FileStream out("testfiles/store/photos/puppy.jpg", O_CREAT | O_WRONLY);
		in.CopyStreamTo(out);
	}

	// This is the example from the Amazon S3 Developers Guide, page 31.
	// Correct, official signature should succeed, with lower-case headers.
	{
		// http://docs.amazonwebservices.com/AmazonS3/2006-03-01/RESTAuthentication.html
		HTTPRequest request(HTTPRequest::Method_GET, "/photos/puppy.jpg");
		request.SetHostName("johnsmith.s3.amazonaws.com");
		request.AddHeader("date", "Tue, 27 Mar 2007 19:36:42 +0000");
		std::string signature = calculate_s3_signature(request,
			EXAMPLE_S3_SECRET_KEY);
		TEST_EQUAL(signature, "xXjDGYUmKxnwqr5KXNPGldn5LbA=");
		request.AddHeader("authorization",
			"AWS " EXAMPLE_S3_ACCESS_KEY ":" + signature);

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
			"AWS " EXAMPLE_S3_ACCESS_KEY ":xXjDGYUmKxnwqr5KXNPGldn5LbB=");

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
			"<p>An error occurred while processing the request:</p>\n"
			"<pre>HTTPException(AuthenticationFailed): "
			"Authentication code mismatch: expected AWS 0PN5J17HBGZHT7JJ3X82"
			":xXjDGYUmKxnwqr5KXNPGldn5LbA= but received AWS "
			"0PN5J17HBGZHT7JJ3X82:xXjDGYUmKxnwqr5KXNPGldn5LbB=</pre>\n"
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
		
		HTTPResponse response = client.GetObject("/photos/puppy.jpg");
		TEST_EQUAL(200, response.GetResponseCode());
		std::string response_data((const char *)response.GetBuffer(),
			response.GetSize());
		TEST_EQUAL("omgpuppies!\n", response_data);

		// make sure that assigning to HTTPResponse does clear stream
		response = client.GetObject("/photos/puppy.jpg");
		TEST_EQUAL(200, response.GetResponseCode());
		response_data = std::string((const char *)response.GetBuffer(),
			response.GetSize());
		TEST_EQUAL("omgpuppies!\n", response_data);

		response = client.GetObject("/nonexist");
		TEST_EQUAL(404, response.GetResponseCode());
		
		FileStream fs("testfiles/testrequests.pl");
		response = client.PutObject("/newfile", fs);
		TEST_EQUAL(200, response.GetResponseCode());

		response = client.GetObject("/newfile");
		TEST_EQUAL(200, response.GetResponseCode());
		TEST_THAT(fs.CompareWith(response));
		TEST_EQUAL(0, ::unlink("testfiles/store/newfile"));
	}

	{
		HTTPRequest request(HTTPRequest::Method_PUT, "/newfile");
		request.SetHostName("quotes.s3.amazonaws.com");
		request.AddHeader("date", "Wed, 01 Mar  2006 12:00:00 GMT");
		// request.AddHeader("Content-Type", "text/plain");

		std::string signature = calculate_s3_signature(request,
			EXAMPLE_S3_SECRET_KEY);
		TEST_EQUAL(signature, "XtMYZf0hdOo4TdPYQknZk0Lz7rw=");
		request.AddHeader("authorization", "AWS " EXAMPLE_S3_ACCESS_KEY ":" +
			signature);

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

		FileStream f1("testfiles/testrequests.pl");
		FileStream f2("testfiles/store/newfile");
		TEST_THAT(f1.CompareWith(f2));
		TEST_EQUAL(0, EMU_UNLINK("testfiles/store/newfile"));
	}

	// Copy testfiles/dsfdsfs98.fd to testfiles/store/dsfdsfs98.fd
	{
		FileStream in("testfiles/dsfdsfs98.fd", O_RDONLY);
		FileStream out("testfiles/store/dsfdsfs98.fd", O_CREAT | O_WRONLY);
		in.CopyStreamTo(out);
	}

	// S3Client tests with S3Simulator in-process server for debugging
	{
		S3Simulator simulator;
		simulator.Configure("testfiles/s3simulator.conf");
		S3Client client(&simulator, "johnsmith.s3.amazonaws.com",
			EXAMPLE_S3_ACCESS_KEY, EXAMPLE_S3_SECRET_KEY);
		TEST_THAT(exercise_s3client(client));
	}

	// Start the S3Simulator server
	pid = StartDaemon(0, TEST_EXECUTABLE " s3server testfiles/s3simulator.conf",
		"testfiles/s3simulator.pid");
	TEST_THAT_OR(pid > 0, return 1);

	// This is the example from the Amazon S3 Developers Guide, page 31
	{
		HTTPRequest request(HTTPRequest::Method_GET, "/photos/puppy.jpg");
		request.SetHostName("johnsmith.s3.amazonaws.com");
		request.AddHeader("date", "Tue, 27 Mar 2007 19:36:42 +0000");
		request.AddHeader("authorization",
			"AWS " EXAMPLE_S3_ACCESS_KEY ":xXjDGYUmKxnwqr5KXNPGldn5LbA=");

		HTTPResponse response;
		TEST_THAT(send_and_receive(request, response));
	}

	// Test that requests for nonexistent files correctly return a 404 error
	{
		HTTPRequest request(HTTPRequest::Method_GET, "/nonexist");
		request.SetHostName("quotes.s3.amazonaws.com");
		request.AddHeader("Date", "Wed, 01 Mar  2006 12:00:00 GMT");
		request.SetClientKeepAliveRequested(true);

		std::string signature = calculate_s3_signature(request,
			EXAMPLE_S3_SECRET_KEY);
		TEST_EQUAL(signature, "0cSX/YPdtXua1aFFpYmH1tc0ajA=");
		request.AddHeader("authorization", "AWS " EXAMPLE_S3_ACCESS_KEY ":" +
			signature);

		HTTPResponse response;
		TEST_THAT(send_and_receive(request, response, 404));
		TEST_THAT(!response.IsKeepAlive());
	}

#ifndef WIN32 // much harder to make files inaccessible on WIN32
	// Make file inaccessible, should cause server to return a 403 error,
	// unless of course the test is run as root :)
	{
		TEST_THAT(chmod("testfiles/store/dsfdsfs98.fd", 0) == 0);
		HTTPRequest request(HTTPRequest::Method_GET, "/dsfdsfs98.fd");
		request.SetHostName("quotes.s3.amazonaws.com");
		request.AddHeader("Date", "Wed, 01 Mar  2006 12:00:00 GMT");
		request.AddHeader("Authorization", "AWS " EXAMPLE_S3_ACCESS_KEY
			":NO9tjQuMCK83z2VZFaJOGKeDi7M=");
		request.SetClientKeepAliveRequested(true);

		HTTPResponse response;
		TEST_THAT(send_and_receive(request, response, 403));
		TEST_THAT(chmod("testfiles/store/dsfdsfs98.fd", 0755) == 0);
	}
#endif

	{
		HTTPRequest request(HTTPRequest::Method_GET, "/dsfdsfs98.fd");
		request.SetHostName("quotes.s3.amazonaws.com");
		request.AddHeader("Date", "Wed, 01 Mar  2006 12:00:00 GMT");
		request.AddHeader("Authorization", "AWS " EXAMPLE_S3_ACCESS_KEY
			":NO9tjQuMCK83z2VZFaJOGKeDi7M=");
		request.SetClientKeepAliveRequested(true);

		HTTPResponse response;
		TEST_THAT(send_and_receive(request, response));

		TEST_EQUAL("qBmKRcEWBBhH6XAqsKU/eg24V3jf/kWKN9dJip1L/FpbYr9FDy7wWFurfdQOEMcY",
			response.GetHeaderValue("x-amz-id-2"));
		TEST_EQUAL("F2A8CCCA26B4B26D", response.GetHeaderValue("x-amz-request-id"));
		TEST_EQUAL("Wed, 01 Mar  2006 12:00:00 GMT", response.GetHeaderValue("Date"));
		TEST_EQUAL("Sun, 1 Jan 2006 12:00:00 GMT", response.GetHeaderValue("Last-Modified"));
		TEST_EQUAL(34, response.GetHeaderValue("ETag").size());
		TEST_EQUAL("text/plain", response.GetContentType());
		TEST_EQUAL("AmazonS3", response.GetHeaderValue("Server"));
		TEST_THAT(!response.IsKeepAlive());

		FileStream file("testfiles/dsfdsfs98.fd");
		TEST_THAT(file.CompareWith(response));
	}

	{
		SocketStream sock;
		sock.Open(Socket::TypeINET, "localhost", 1080);

		HTTPRequest request(HTTPRequest::Method_PUT, "/newfile");
		request.SetHostName("quotes.s3.amazonaws.com");
		request.AddHeader("Date", "Wed, 01 Mar  2006 12:00:00 GMT");
		request.AddHeader("Authorization", "AWS " EXAMPLE_S3_ACCESS_KEY
			":kfY1m6V3zTufRy2kj92FpQGKz4M=");
		request.AddHeader("Content-Type", "text/plain");
		FileStream fs("testfiles/dsfdsfs98.fd");
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

		FileStream f1("testfiles/dsfdsfs98.fd");
		FileStream f2("testfiles/store/newfile");
		TEST_THAT(f1.CompareWith(f2));
		TEST_THAT(EMU_UNLINK("testfiles/store/newfile") == 0);
	}

	// S3Client tests with S3Simulator daemon for realism
	{
		S3Client client("localhost", 1080, EXAMPLE_S3_ACCESS_KEY,
			EXAMPLE_S3_SECRET_KEY);
		TEST_THAT(exercise_s3client(client));
	}

	// Test the HTTPQueryDecoder::URLEncode method.
	TEST_EQUAL("AZaz09-_.~", HTTPQueryDecoder::URLEncode("AZaz09-_.~"));
	TEST_EQUAL("%00%01%FF",
		HTTPQueryDecoder::URLEncode(std::string("\0\x01\xff", 3)));

	// Kill it
	TEST_THAT(StopDaemon(pid, "testfiles/s3simulator.pid",
		"s3simulator.memleaks", true));

	TEST_THAT(StartSimulator());

	// S3Client tests with s3simulator executable for even more realism
	{
		S3Client client("localhost", 1080, EXAMPLE_S3_ACCESS_KEY,
			EXAMPLE_S3_SECRET_KEY);
		TEST_THAT(exercise_s3client(client));
	}

	TEST_THAT(StopSimulator());

	TEARDOWN();
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

	TEST_THAT(test_httpserver());

	return finish_test_suite();
}
