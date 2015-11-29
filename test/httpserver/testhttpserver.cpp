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
#include <cstdlib>
#include <cstring>
#include <ctime>

#ifdef HAVE_SIGNAL_H
	#include <signal.h>
#endif

#include <boost/foreach.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <openssl/x509.h>
#include <openssl/hmac.h>

#include "autogen_HTTPException.h"
#include "HTTPQueryDecoder.h"
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

using boost::property_tree::ptree;

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

// http://docs.aws.amazon.com/AmazonSimpleDB/latest/DeveloperGuide/HMACAuth.html
std::string generate_query_string(const HTTPRequest& request)
{
	std::vector<std::string> param_names;
	std::map<std::string, std::string> param_values;

	const HTTPRequest::Query_t& params(request.GetQuery());
	for(HTTPRequest::Query_t::const_iterator i = params.begin();
		i != params.end(); i++)
	{
		// We don't want to include the Signature parameter in the sorted query
		// string, because the client didn't either when computing the signature!
		if(i->first != "Signature")
		{
			param_names.push_back(i->first);
			// This algorithm only supports non-repeated parameters, so
			// assert that we don't already have a parameter with this name.
			TEST_LINE_OR(param_values.find(i->first) == param_values.end(),
				"Multiple values for parameter '" << i->first << "'",
				return "");
			param_values[i->first] = i->second;
		}
	}

	std::sort(param_names.begin(), param_names.end());
	std::ostringstream out;

	for(std::vector<std::string>::iterator i = param_names.begin();
		i != param_names.end(); i++)
	{
		if(i != param_names.begin())
		{
			out << "&";
		}
		out << HTTPQueryDecoder::URLEncode(*i) << "=" <<
			HTTPQueryDecoder::URLEncode(param_values[*i]);
	}

	return out.str();
}

// http://docs.aws.amazon.com/AmazonSimpleDB/latest/DeveloperGuide/HMACAuth.html
std::string calculate_simpledb_signature(const HTTPRequest& request,
	const std::string& aws_secret_access_key)
{
	// This code is very similar to that in S3Client::FinishAndSendRequest,
	// but using EVP_sha256 instead of EVP_sha1. TODO FIXME: factor out the
	// common parts.
	std::string query_string = generate_query_string(request);
	TEST_THAT_OR(query_string != "", return "");

	std::ostringstream buffer_to_sign;
	buffer_to_sign << request.GetMethodName() << "\n" <<
		request.GetHeaders().GetHostNameWithPort() << "\n" <<
		// The HTTPRequestURI component is the HTTP absolute path component
		// of the URI up to, but not including, the query string. If the 
		// HTTPRequestURI is empty, use a forward slash ( / ).
		request.GetRequestURI() << "\n" <<
		query_string;

	// Thanks to https://gist.github.com/tsupo/112188:
	unsigned int digest_size;
	unsigned char digest_buffer[EVP_MAX_MD_SIZE];
	std::string string_to_sign = buffer_to_sign.str();

	HMAC(EVP_sha256(),
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

bool add_simpledb_signature(HTTPRequest& request, const std::string& aws_secret_access_key)
{
	std::string signature = calculate_simpledb_signature(request,
		aws_secret_access_key);
	request.SetParameter("Signature", signature);
	return !signature.empty();
}

bool send_and_receive(HTTPRequest& request, HTTPResponse& response,
	int expected_status_code = 200)
{
	SocketStream sock;
	sock.Open(Socket::TypeINET, "localhost", 1080);
	TEST_THAT(request.Send(sock, LONG_TIMEOUT));

	response.Reset();
	response.Receive(sock, LONG_TIMEOUT);
	std::string response_data((const char *)response.GetBuffer(),
		response.GetSize());
	TEST_EQUAL_LINE(expected_status_code, response.GetResponseCode(),
		response_data);
	return (response.GetResponseCode() == expected_status_code);
}

bool send_and_receive_xml(HTTPRequest& request, ptree& response_tree,
	const std::string& expected_root_element)
{
	HTTPResponse response;
	TEST_THAT_OR(send_and_receive(request, response), return false);

	std::string response_data((const char *)response.GetBuffer(),
		response.GetSize());
	std::auto_ptr<std::istringstream> ap_response_stream(
		new std::istringstream(response_data));
	read_xml(*ap_response_stream, response_tree,
		boost::property_tree::xml_parser::trim_whitespace);

	TEST_EQUAL_OR(expected_root_element, response_tree.begin()->first, return false);
	TEST_LINE(++(response_tree.begin()) == response_tree.end(),
		"There should only be one item in the response tree root");

	return true;
}

std::vector<std::string> simpledb_list_domains(const std::string& access_key,
	const std::string& secret_key)
{
	HTTPRequest request(HTTPRequest::Method_GET, "/");
	request.SetHostName("sdb.eu-west-1.amazonaws.com");
	request.AddParameter("Action", "ListDomains");
	request.AddParameter("AWSAccessKeyId", access_key);
	request.AddParameter("SignatureVersion", "2");
	request.AddParameter("SignatureMethod", "HmacSHA256");
	request.AddParameter("Timestamp", "2010-01-25T15:01:28-07:00");
	request.AddParameter("Version", "2009-04-15");

	TEST_THAT_OR(add_simpledb_signature(request, secret_key),
		return std::vector<std::string>());

	ptree response_tree;
	TEST_THAT(send_and_receive_xml(request, response_tree, "ListDomainsResponse"));

	std::vector<std::string> domains;
	BOOST_FOREACH(ptree::value_type &v,
		response_tree.get_child("ListDomainsResponse.ListDomainsResult"))
	{
		domains.push_back(v.second.data());
	}

	return domains;
}

bool simpledb_get_attributes(const std::string& access_key, const std::string& secret_key,
	const std::multimap<std::string, std::string> const_attributes)
{
	HTTPRequest request(HTTPRequest::Method_GET, "/");
	request.SetHostName("sdb.eu-west-1.amazonaws.com");
	request.AddParameter("Action", "GetAttributes");
	request.AddParameter("DomainName", "MyDomain");
	request.AddParameter("ItemName", "JumboFez");
	request.AddParameter("AWSAccessKeyId", access_key);
	request.AddParameter("SignatureVersion", "2");
	request.AddParameter("SignatureMethod", "HmacSHA256");
	request.AddParameter("Timestamp", "2010-01-25T15:01:28-07:00");
	request.AddParameter("Version", "2009-04-15");
	TEST_THAT_OR(add_simpledb_signature(request, secret_key), return false);

	ptree response_tree;
	TEST_THAT(send_and_receive_xml(request, response_tree,
		"GetAttributesResponse"));

	// Check that all attributes were written correctly
	std::multimap<std::string, std::string> attributes = const_attributes;
	TEST_EQUAL_LINE(const_attributes.size(),
		response_tree.get_child("GetAttributesResponse.GetAttributesResult").size(),
		"Wrong number of attributes in response");

	bool all_match = (const_attributes.size() ==
		response_tree.get_child("GetAttributesResponse.GetAttributesResult").size());

	std::multimap<std::string, std::string>::iterator i = attributes.begin();
	BOOST_FOREACH(ptree::value_type &v,
		response_tree.get_child(
			"GetAttributesResponse.GetAttributesResult"))
	{
		std::string name = v.second.get<std::string>("Name");
		std::string value = v.second.get<std::string>("Value");
		if(i == attributes.end())
		{
			TEST_EQUAL_LINE("", name, "Unexpected attribute name");
			TEST_EQUAL_LINE("", value, "Unexpected attribute value");
			all_match = false;
		}
		else
		{
			TEST_EQUAL_LINE(i->first, name, "Wrong attribute name");
			TEST_EQUAL_LINE(i->second, value, "Wrong attribute value");
			all_match &= (i->first == name);
			all_match &= (i->second == value);
			i++;
		}
	}

	return all_match;
}

bool compare_lists(const std::vector<std::string>& expected_items,
	const std::vector<std::string>& actual_items)
{
	bool all_match = (expected_items.size() == actual_items.size());

	for(size_t i = 0; i < std::max(expected_items.size(), actual_items.size()); i++)
	{
		const std::string& expected = (i < expected_items.size()) ? expected_items[i] : "None";
		const std::string& actual   = (i < actual_items.size())   ? actual_items[i]   : "None";
		TEST_EQUAL_LINE(expected, actual, "Item " << i);
		all_match &= (expected == actual);
	}

	return all_match;
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

	TEST_THAT(system("rm -rf *.memleaks testfiles/domains.qdbm testfiles/items.qdbm")
		== 0);

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
		const HTTPHeaders& headers(request2.GetHeaders());
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
			"<p>An error occurred while processing the request:</p>\n"
			"<pre>HTTPException(AuthenticationFailed): "
			"Authentication code mismatch</pre>\n"
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
		HTTPRequest request(HTTPRequest::Method_GET, "/photos/puppy.jpg");
		request.SetHostName("johnsmith.s3.amazonaws.com");
		request.AddHeader("date", "Tue, 27 Mar 2007 19:36:42 +0000");
		request.AddHeader("authorization",
			"AWS 0PN5J17HBGZHT7JJ3X82:xXjDGYUmKxnwqr5KXNPGldn5LbA=");

		HTTPResponse response;
		TEST_THAT(send_and_receive(request, response));
	}

	{
		HTTPRequest request(HTTPRequest::Method_GET, "/nonexist");
		request.SetHostName("quotes.s3.amazonaws.com");
		request.AddHeader("Date", "Wed, 01 Mar  2006 12:00:00 GMT");
		request.AddHeader("Authorization", "AWS 0PN5J17HBGZHT7JJ3X82:0cSX/YPdtXua1aFFpYmH1tc0ajA=");
		request.SetClientKeepAliveRequested(true);

		HTTPResponse response;
		TEST_THAT(send_and_receive(request, response, 404));
		TEST_THAT(!response.IsKeepAlive());
	}

	#ifndef WIN32 // much harder to make files inaccessible on WIN32
	// Make file inaccessible, should cause server to return a 403 error,
	// unless of course the test is run as root :)
	{
		TEST_THAT(chmod("testfiles/testrequests.pl", 0) == 0);
		HTTPRequest request(HTTPRequest::Method_GET,
			"/testrequests.pl");
		request.SetHostName("quotes.s3.amazonaws.com");
		request.AddHeader("Date", "Wed, 01 Mar  2006 12:00:00 GMT");
		request.AddHeader("Authorization", "AWS 0PN5J17HBGZHT7JJ3X82:qc1e8u8TVl2BpIxwZwsursIb8U8=");
		request.SetClientKeepAliveRequested(true);

		HTTPResponse response;
		TEST_THAT(send_and_receive(request, response, 403));
		TEST_THAT(chmod("testfiles/testrequests.pl", 0755) == 0);
	}
	#endif

	{
		HTTPRequest request(HTTPRequest::Method_GET,
			"/testrequests.pl");
		request.SetHostName("quotes.s3.amazonaws.com");
		request.AddHeader("Date", "Wed, 01 Mar  2006 12:00:00 GMT");
		request.AddHeader("Authorization", "AWS 0PN5J17HBGZHT7JJ3X82:qc1e8u8TVl2BpIxwZwsursIb8U8=");
		request.SetClientKeepAliveRequested(true);

		HTTPResponse response;
		TEST_THAT(send_and_receive(request, response));

		TEST_EQUAL("qBmKRcEWBBhH6XAqsKU/eg24V3jf/kWKN9dJip1L/FpbYr9FDy7wWFurfdQOEMcY",
			response.GetHeaderValue("x-amz-id-2"));
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

	// Test the HTTPQueryDecoder::URLEncode method.
	TEST_EQUAL("AZaz09-_.~", HTTPQueryDecoder::URLEncode("AZaz09-_.~"));
	TEST_EQUAL("%00%01%FF",
		HTTPQueryDecoder::URLEncode(std::string("\0\x01\xff", 3)));

	// Test that we can calculate the correct signature for a known request:
	// http://docs.aws.amazon.com/AWSECommerceService/latest/DG/rest-signature.html
	{
		HTTPRequest request(HTTPRequest::Method_GET, "/onca/xml");
		request.SetHostName("webservices.amazon.com");
		request.AddParameter("Service", "AWSECommerceService");
		request.AddParameter("AWSAccessKeyId", "AKIAIOSFODNN7EXAMPLE");
		request.AddParameter("AssociateTag", "mytag-20");
		request.AddParameter("Operation", "ItemLookup");
		request.AddParameter("ItemId", "0679722769");
		request.AddParameter("ResponseGroup",
			"Images,ItemAttributes,Offers,Reviews");
		request.AddParameter("Version", "2013-08-01");
		request.AddParameter("Timestamp", "2014-08-18T12:00:00Z");

		std::string auth_code = calculate_simpledb_signature(request,
			"1234567890");
		TEST_EQUAL("j7bZM0LXZ9eXeZruTqWm2DIvDYVUU3wxPPpp+iXxzQc=", auth_code);
	}

	// Test the S3Simulator's implementation of SimpleDB
	{
		std::string access_key = "0PN5J17HBGZHT7JJ3X82";
		std::string secret_key = "uV3F3YluFJax1cknvbcGwgjvx4QpvB+leU8dUj2o";

		HTTPRequest request(HTTPRequest::Method_GET, "/");
		request.SetHostName("sdb.eu-west-1.amazonaws.com");
		request.AddParameter("Action", "ListDomains");
		request.AddParameter("AWSAccessKeyId", access_key);
		request.AddParameter("SignatureVersion", "2");
		request.AddParameter("SignatureMethod", "HmacSHA256");
		request.AddParameter("Timestamp", "2010-01-25T15:01:28-07:00");
		request.AddParameter("Version", "2009-04-15");
		TEST_THAT(add_simpledb_signature(request, secret_key));

		// Send directly to in-process simulator, useful for debugging.
		// CollectInBufferStream response_buffer;
		// HTTPResponse response(&response_buffer);
		HTTPResponse response;

		S3Simulator simulator;
		simulator.Configure("testfiles/s3simulator.conf");
		simulator.Handle(request, response);
		std::string response_data((const char *)response.GetBuffer(),
			response.GetSize());
		TEST_EQUAL_LINE(200, response.GetResponseCode(), response_data);

		// Send to out-of-process simulator, useful for testing HTTP
		// implementation.
		TEST_THAT(send_and_receive(request, response));

		// Check that there are no existing domains at the start
		std::vector<std::string> domains = simpledb_list_domains(access_key, secret_key);
		std::vector<std::string> expected_domains;
		TEST_THAT(compare_lists(expected_domains, domains));

		// Create a domain
		request.SetParameter("Action", "CreateDomain");
		request.SetParameter("DomainName", "MyDomain");
		TEST_THAT(add_simpledb_signature(request, secret_key));

		ptree response_tree;
		TEST_THAT(send_and_receive_xml(request, response_tree,
			"CreateDomainResponse"));

		// List domains again, check that our new domain is present.
		domains = simpledb_list_domains(access_key, secret_key);
		expected_domains.push_back("MyDomain");
		TEST_THAT(compare_lists(expected_domains, domains));

		// Create an item
		request.SetParameter("Action", "PutAttributes");
		request.SetParameter("DomainName", "MyDomain");
		request.SetParameter("ItemName", "JumboFez");
		request.SetParameter("Attribute.1.Name", "Color");
		request.SetParameter("Attribute.1.Value", "Blue");
		request.SetParameter("Attribute.2.Name", "Size");
		request.SetParameter("Attribute.2.Value", "Med");
		TEST_THAT(add_simpledb_signature(request, secret_key));
		TEST_THAT(send_and_receive_xml(request, response_tree,
			"PutAttributesResponse"));

		// Get the item back, and check that all attributes were written
		// correctly.
		std::multimap<std::string, std::string> expected_attrs;
		typedef std::multimap<std::string, std::string>::value_type attr_t;
		expected_attrs.insert(attr_t("Color", "Blue"));
		expected_attrs.insert(attr_t("Size", "Med"));
		TEST_THAT(simpledb_get_attributes(access_key, secret_key, expected_attrs));

		// Add more attributes. The Size attribute is added with the Replace
		// option, so it replaces the previous value. The Color attribute is not,
		// so it adds another value.
		request.SetParameter("Attribute.1.Value", "Not Blue");
		request.SetParameter("Attribute.1.Replace", "true");
		request.SetParameter("Attribute.2.Value", "Large");
		TEST_THAT(add_simpledb_signature(request, secret_key));
		TEST_THAT(send_and_receive_xml(request, response_tree,
			"PutAttributesResponse"));

		// Check that all attributes were written correctly, by getting the item
		// again.
		expected_attrs.erase("Color");
		expected_attrs.insert(attr_t("Color", "Not Blue"));
		expected_attrs.insert(attr_t("Size", "Large"));
		TEST_THAT(simpledb_get_attributes(access_key, secret_key, expected_attrs));

		// Conditional PutAttributes that fails (doesn't match) and therefore
		// doesn't change anything.
		request.SetParameter("Attribute.1.Value", "Green");
		request.SetParameter("Attribute.2.Replace", "true");
		request.SetParameter("Expected.1.Name", "Color");
		request.SetParameter("Expected.1.Value", "What?");
		TEST_THAT(add_simpledb_signature(request, secret_key));
		TEST_THAT(send_and_receive(request, response, HTTPResponse::Code_Conflict));
		TEST_THAT(simpledb_get_attributes(access_key, secret_key, expected_attrs));

		// Conditional PutAttributes again, with the correct value for the Color
		// attribute this time, so the request should succeed.
		request.SetParameter("Expected.1.Value", "Not Blue");
		TEST_THAT(add_simpledb_signature(request, secret_key));
		TEST_THAT(send_and_receive_xml(request, response_tree,
			"PutAttributesResponse"));

		// If it does, because Replace is set for the Size parameter as well, both
		// Size values will be replaced by the new single value.
		expected_attrs.clear();
		expected_attrs.insert(attr_t("Color", "Green"));
		expected_attrs.insert(attr_t("Size", "Large"));
		TEST_THAT(simpledb_get_attributes(access_key, secret_key, expected_attrs));
	}

	// Kill it
	TEST_THAT(StopDaemon(pid, "testfiles/s3simulator.pid",
		"s3simulator.memleaks", true));

	return 0;
}

